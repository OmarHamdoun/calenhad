//f
#include "CalenhadModel.h"
#include "../CalenhadServices.h"
#include "../nodeedit/CalenhadController.h"
#include "../nodeedit/CalenhadView.h"
#include "nodeedit/Connection.h"
#include "nodeedit/NodeBlock.h"
#include "qmodule/NodeGroup.h"
#include "../icosphere/icosphere.h"
#include "../pipeline/ModuleFactory.h"
#include "../actions/XmlCommand.h"
#include "nodeedit/Port.h"
#include "qmodule/Module.h"
#include "exprtk/Calculator.h"
#include <QGraphicsSceneMouseEvent>
#include <actions/ContextAction.h>
#include "../legend/LegendService.h"
#include "../preferences/PreferencesService.h"
#include <QList>
#include <actions/XmlCommand.h>
#include <QGraphicsItem>
#include <QtGui/QGuiApplication>
#include <QClipboard>

using namespace icosphere;
using namespace calenhad;
using namespace calenhad::pipeline;
using namespace calenhad::nodeedit;
using namespace calenhad::qmodule;
using namespace calenhad::actions;
using namespace calenhad::expressions;
using namespace calenhad::preferences;
using namespace calenhad::legend;
using namespace calenhad::notification;

CalenhadModel::CalenhadModel() : QGraphicsScene(),
    conn (nullptr),
    _port (nullptr),
    _author (""),
    _title ("New model"),
    _description (""),
    _date (QDateTime::currentDateTime()),
    _controller (nullptr),
    _highlighted (nullptr),
    _menu (nullptr),
    _changed (false),
    _existingConnection (false),
    _wasConnectedTo (nullptr),
    _filename (""),
    _undoEnabled (true),
    _lastSaved (QDateTime::currentDateTime()) {
    installEventFilter (this);
    connect (CalenhadServices::legends(), &LegendService::commitRequested, this, &CalenhadModel::commitLegends);
    connect (CalenhadServices::legends(), &LegendService::rollbackRequested, this, &CalenhadModel::rollbackLegends);
    _connectMenu = new QMenu();
    _connectSubMenu = new QMenu (_connectMenu);
    double extent = CalenhadServices::preferences() -> calenhad_model_extent / 2;
    setSceneRect (QRectF (-extent / 2, -extent / 2, extent, extent));
    // Load legends from default legends file
    QString file = CalenhadServices::preferences() -> calenhad_legends_filename;
    inflate (file, CalenhadFileType::CalenhadLegendFile);
}


CalenhadModel::~CalenhadModel() {
    // if we don't turn off the signals on the nodes it will keep trying to render them whilst connections are being removed by the destructor.
    for (Node* n : nodes()) {
        n -> blockSignals (true);
    }
    if (_menu) { delete _menu; }
}

// determine whether connection from given input to given output is allowed
bool CalenhadModel::canConnect (Port* output, Port* input, const bool& verbose) {
    //To do: externalise message strings
    if (output && input) {

        // can't connect a block to itself
        if (output -> block() == input -> block()) {
            if (verbose) {
                CalenhadServices::messages() -> message ("Cannot connect", "Cannot connect owner to itself", NotificationStyle::ErrorNotification);
            }
            return false;
        }

        // can only connect an output port to an input port
        if (input -> portType() ==  Port::OutputPort) {
            if (verbose) {
                CalenhadServices::messages() -> message ("Cannot connect", "Cannot make connection to another owner output", NotificationStyle::ErrorNotification);
            }
            return false;
        }

        // can't create circular paths so reject if a path already exists from proposed output back to proposed input
        // (in which case this connection would complete a circle)
        if (existsPath (output -> owner() -> handle(), input -> owner() -> handle())) {
            if (verbose) {
                CalenhadServices::messages() -> message ("Cannot connect", "Connection would form a circuit within the network", NotificationStyle::ErrorNotification);
            }
            return false;
        }

        // can't connect to a port that's already connected to another output
        if (! (input -> connections().empty())) {
            if (verbose) {
                CalenhadServices::messages() -> message ("Cannot connect", "Port is already connected", NotificationStyle::ErrorNotification);
                return false;
            }
        }

        // if survived all that, connection is OK
        return true;
    } else {
        emit showMessage ("No target node selected");
        return false;
    }
};

// Return true if there is a direct or indirect path from a given output port to a given input port.
// If there would be a path from output to input on the same node we can't create a new connection between them as that would complete a circuit.
bool CalenhadModel::existsPath (NodeBlock* output, NodeBlock* input) {

    Node* outputNode = output -> node ();
    Port* outputPort = output -> output();
    if (outputPort) {
        Module* outputModule = dynamic_cast<Module*> (outputNode);
        Module* inputModule = dynamic_cast<Module*> (input->node ());
        if (outputPort->connections ().isEmpty ()) {
            return false;
        } else {
            Connection* connection = outputPort->connections ()[0];
            // we're only interested in QModules here
            if ((!outputModule) || (!inputModule)) {
                return false;
            }

            // base case: block with no inputs can't have any paths to it
            if (inputModule->handle ()->inputs ().isEmpty ()) {
                return false;

                // see if the two blocks are connected
            } else {
                for (Port* inputPort: inputModule->handle ()->inputs ()) {
                    if (connection->otherEnd (outputPort) == inputPort) {
                        return true;
                    } else {
                        if (!(inputPort->connections ().isEmpty ())) {
                            Connection* c = inputPort->connections ()[0];
                            return existsPath (outputPort->block (), c->otherEnd (inputPort)->block ());
                        }
                    }
                }
            }
        }
        return false;
    } else {
        return false;
    }
}

Connection* CalenhadModel::doConnectPorts (Port* output, Port* input) {

    if (canConnect (output, input, true)) {
        preserve();
        Connection* c = new Connection();
        c -> setParentItem (0);
        c -> setZValue (-900);
        addItem (c);
        c -> setPort1 (output);
        c -> setPort2 (input);
        c -> updatePosFromPorts();
        c -> updatePath();

        // tell the target owner to declare change requiring rerender
        output -> owner() -> invalidate();

        // this propogates changes on the source owner to the target so that the target can update any visible views when its inputs change
        connect (output -> owner(), SIGNAL (nodeChanged()), input -> owner(), SLOT (invalidate()));

        // colour the input to show its connected status
        input -> setHighlight (Port::PortHighlight::CONNECTED);

        // tell the target owner to declare change requiring rerender
        output -> owner() -> invalidate();

        // model has changed so save state
        setChanged();
        setRestorePoint();

        return c;
    } else {
        return nullptr;
    }
}

void CalenhadModel::doDisconnectPorts (Connection* connection) {
    if (connection -> port1()) { connection -> port1() -> initialise(); }
    if (connection -> port2()) { connection -> port2() -> initialise(); }

    // colour the input port to show its availability
    if (connection -> port1() -> type() != Port::OutputPort) { connection -> port1() -> setHighlight (Port::PortHighlight::NONE); }
    if (connection -> port2() -> type() != Port::OutputPort) { connection -> port2() -> setHighlight (Port::PortHighlight::NONE); }

    // reproduce the renders to reflect the change
    //    connection -> port2() -> invalidateRenders();

    // update the model
    preserve();
    removeItem (connection);
    delete connection;
    setChanged();
    setRestorePoint();
    update();
}

// This handler is required to stop a right-click which brings up the context menu from clearing the selection.
// Left clicking in space however should discard any current selection.
void CalenhadModel::mousePressEvent (QGraphicsSceneMouseEvent *event) {
    if (event -> button() != Qt::LeftButton) {
        event -> accept();
        return;
    }
    QGraphicsScene::mousePressEvent (event);
}

bool CalenhadModel::eventFilter (QObject* o, QEvent* e) {

    QGraphicsSceneMouseEvent* me = (QGraphicsSceneMouseEvent*) e;
    switch ((int) e -> type()) {

        case QEvent::GraphicsSceneMousePress: {
            preserve();
            switch ((int) me -> button()) {
                case Qt::LeftButton: {
                    QPointF pos = me -> scenePos();
                    QList<QGraphicsItem*> items = QGraphicsScene::items (pos) ;
                    foreach (QGraphicsItem* item, items) {

                        // click on an output port - create a connection which we can connect to another owner's input or control port
                        if (item && item -> type() == Port::Type) {
                            for (QGraphicsView* view : views()) {
                                view -> setDragMode (QGraphicsView::NoDrag);
                            }
                            // only allow connections from output ports to input ports
                            Port* port = ((Port*) item);
                            if (conn) { delete conn; }
                            if (port->portType() == Port::OutputPort) {


                                conn = new Connection (0);
                                addItem (conn);
                                conn -> setPort1 ((Port*) item);
                                conn -> setPos1 (item -> scenePos());
                                conn -> setPos2 (me->scenePos());
                                conn -> updatePath();
                                conn -> canDrop = false;
                                _existingConnection = false;
                            } else {
                                // dragging an existing connection off an input/control port makes the end floating again so we can drop it somewhere else
                                if ( ! port -> connections().isEmpty()) {
                                    conn = port -> connections().first();
                                    _wasConnectedTo = conn -> port2();
                                    conn -> setPort2 (nullptr);
                                    _existingConnection = true;
                                    return true;
                                }
                            }
                        }
                    }
                    break;
                }

                case Qt::RightButton: {

                    if (! conn) {
                        QList<QGraphicsItem*> items = QGraphicsScene::items (me -> scenePos());
                        if (items.empty()) {
                            QMenu* menu = makeMenu (nullptr);
                            if (menu) {
                                menu -> exec (me->screenPos());
                            }
                        } else {
                            foreach (QGraphicsItem* item, items) {
                                QMenu* menu = makeMenu (item);
                                if (menu) {
                                    menu -> exec (me->screenPos());
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
        case QEvent::GraphicsSceneMouseMove: {

            // if we move over a nodegroup, highlight it
            highlightGroupAt (me -> scenePos());

            // if we moved off a port, set it back to its ordinary style
            if (_port) {
                _port -> initialise();
            }
            if (conn) {
               // _viewToolsGroup -> toolToggled (false);
                conn -> setParentItem (0);
                conn -> setZValue (1000);

                conn -> setPos2 (me -> scenePos());
                conn -> updatePath();
                conn -> canDrop = false;

                QList<QGraphicsItem*> items = QGraphicsScene::items (me -> scenePos());
                foreach (QGraphicsItem* item, items) {
                        if (item && item->type() == Port::Type) {
                            Port* port = (Port*) item;
                            if (port != conn -> port1() && !(port->hasConnection())) {
                                if (canConnect (conn -> port1(), port)) {
                                    // Change colour of a port if we mouse over it and can make a connection to it
                                    port -> setHighlight (Port::PortHighlight::CAN_CONNECT);
                                    _port = port;
                                }
                            }
                        } else {
                            if (_port) {
                                // If we moved off a port without making a connection to it, set it back to its unoccupied colour
                                _port -> setHighlight (Port::PortHighlight::NONE);
                                _port = nullptr;
                            }
                        }
                    }
                update();
                return true;
            } else {
                if (! _activeTool) {
                    for (QGraphicsView* view : views()) {
                        view -> viewport() -> setCursor (Qt::ArrowCursor);
                        for (NodeGroup* group : nodeGroups()) {
                            group -> handle() -> setCursor (Qt::ClosedHandCursor);
                        }
                    }
                }
            }
            break;
        }

        case QEvent::GraphicsSceneMouseRelease: {
            for (QGraphicsItem* item : items()) {
                NodeGroupBlock* block = dynamic_cast<NodeGroupBlock*> (item);
                if (block) {
                    block->setHighlight (false);
                }
            }


            for (QGraphicsView* view : views()) {
                view -> setDragMode (QGraphicsView::RubberBandDrag);
            }
            lastClick = me -> scenePos();
            if (conn) {
                if (_port) {
                    _port -> initialise();
                    _port -> update();
                }
                if (me -> button() == Qt::LeftButton) {
                    QList<QGraphicsItem*> items = QGraphicsScene::items (me -> scenePos());
                    foreach (QGraphicsItem* item, items) {
                        if (item && item->type() == Port::Type) {
                            Port* port1 = conn -> port1();
                            Port* port2 = (Port*) item;
                            createConnection (port1, port2);
                        }
                    }
                }
            } else {
                setRestorePoint();
            }
            if (_activeTool) {
                QString type = _activeTool -> data().toString();
                if (! type.isNull()) {
                    doCreateNode (me->scenePos (), type);
                }
                ((Calenhad*) _controller -> parent()) -> clearTools();
                setActiveTool (nullptr);
            }
            _wasConnectedTo = nullptr;
        }

        // deactivate any in-progress connection whenever a mouse release occurs
        if (conn) {
            delete conn;
            conn = nullptr;
        }
    }

    return QObject::eventFilter (o, e);
}

Node* CalenhadModel::doCreateNode (const QPointF& initPos, const QString& type) {
    preserve();
    QString name = "New_" + type;
    int i = 0;
    Node* n;
    if (type == "nodegroup") {
        n = addNodeGroup (initPos, name);
    } else {
        n = addModule (initPos, type, name);
    }
    setChanged();

    QString newXml = snapshot();
    XmlCommand* command = new XmlCommand (this, _oldXml);
    _controller -> doCommand (command);
    command -> setNewXml (newXml);
    return n;
}

Module* CalenhadModel::addModule (const QPointF& initPos, const QString& type, const QString& name) {
    if (type != QString::null) {
        Module* module = (Module*) CalenhadServices::modules() -> createModule (type, this);
        module -> setName (uniqueName (name));
        module -> setLegend (CalenhadServices::legends() -> lastUsed());
        addNode (module, initPos);
       return module;
    } else {
        CalenhadServices::messages() -> message ("No such type", "Couldn't create module of type " + type, NotificationStyle::ErrorNotification);
        return nullptr;
    }
}

NodeGroup* CalenhadModel::addNodeGroup (const QPointF& initPos, const QString& name) {
    NodeGroup* group = new NodeGroup();
    group -> setModel (this);
    group -> initialise();
    addNode (group, initPos);
    group -> setName (uniqueName (name));
    return group;
}

NodeGroup* CalenhadModel::doAddNodeGroup (const QPainterPath& path) {
    preserve();
    QString name = uniqueName ("New nodegroup");
    NodeGroup* group = addNodeGroup (path.boundingRect().topLeft(), name);
    ((NodeGroupBlock*) group -> handle()) -> resize (path.boundingRect());
    setChanged (true);
    setRestorePoint();
    return group;
}

Node* CalenhadModel::addNode (Node* node, const QPointF& initPos) {
    NodeBlock* b = node -> makeHandle();
    addItem (b);
    connect (node, &Node::nameChanged, b, &NodeBlock::nodeChanged);

    Module* m = dynamic_cast<Module*> (node);
    if (m) {
        for (Port* port : m -> ports ()) {
            b -> addPort (port);
        }
    }
    b -> assignGroup();
    b -> assignIcon();

    if (node -> group()) {
        node -> group() -> handle() -> setSelected (false);
    }

    b -> setPos (initPos.x(), initPos.y());
    return node;
}

bool CalenhadModel::nameExists (const QString& name) {
    for (Node* n : nodes()) {
        if (name == n -> name()) {
            return true;
        }
    }
    return false;
}

void CalenhadModel::doDeleteNode (Node* node) {
    // first delete any connections to or from the module
    preserve();
    for (QGraphicsItem* item : items()) {
        if (item -> type() == Connection::Type) {
            Module* m = dynamic_cast<Module*> (node);
            if (m) {
                for (Port* p : m -> ports ()) {
                    if (((Connection*) item)->port1 () == p || ((Connection*) item)->port2 () == p) {
                        removeItem (item);
                        delete item;
                        setChanged();
                    }
                }
            }
        }
    }

    noise::module::Module* m = nullptr;
    Module* module = dynamic_cast<Module*> (node);

    // remove the visible appartions from the display

    for (QGraphicsItem* item : items()) {
        if (item -> type() == NodeBlock::Type && ((NodeBlock*) item) -> node() == node) {
            removeItem (item -> parentItem());
            delete item;
        }
    }
    delete node;
    update();
    setRestorePoint();
    _port = nullptr; // otherwise it keeps trying to do stuff to the deleted port
}

QList<NodeGroup*> CalenhadModel::nodeGroups() {
    QList<NodeGroup*> groups;
            foreach (QGraphicsItem* item, items()) {
            int type = item -> type();
            if (type == QGraphicsItem::UserType + 3) {  // is a NodeBlock
                NodeBlock* handle = (NodeBlock*) item;
                Node* node = handle -> node();
                if (dynamic_cast<NodeGroup*> (node)) {
                    groups.append (dynamic_cast<NodeGroup*> (node));
                }
            }
        }
    return groups;
}

QList<Node*> CalenhadModel::nodes() {
    QList<Node*> nodes;
            foreach (QGraphicsItem* item, items()) {
            int type = item -> type();
            if (type == QGraphicsItem::UserType + 3) {  // is a NodeBlock
                NodeBlock* handle = (NodeBlock*) item;
                Node* node = handle -> node();
                nodes.append (node);
            }
        }
    return nodes;
}

QList<Connection*> CalenhadModel::connections() {
    QList<Connection*> connections;
            foreach (QGraphicsItem* item, items()) {
            int type = item -> type();
            if (type == QGraphicsItem::UserType + 2) {  // is a Connection
                Connection* c = (Connection*) item;
                connections.append (c);
            }
        }
    return connections;
}


NodeGroup* CalenhadModel::findGroup (const QString& name) {
    for (NodeGroup* qm : nodeGroups()) {
        if ((! qm -> name().isNull()) && (qm -> name() == name)) {
            return qm;
        }
    }
    return nullptr;
}

void CalenhadModel::assignGroups() {
// work out resulting group memberships
    for (Node* n : nodes ()) {
        n->handle ()->assignGroup ();
    }
}


Module* CalenhadModel::findModule (const QString& name) {
    for (Node* qm : nodes()) {
        if ((! qm -> name().isNull()) && (qm -> name() == name)) {
            Module* m = dynamic_cast<Module*> (qm);
            return m ? m : nullptr;
        }
    }
    return nullptr;
}

void CalenhadModel::setActiveTool (QAction* tool) {
    _activeTool = tool;
}

void CalenhadModel::setController (CalenhadController* controller) {
    if (! _controller)  {
        _controller = controller;
    } else {

    }
}

CalenhadController* CalenhadModel::controller() {
    return _controller;
}


void CalenhadModel::serialize (const QString& filename, const CalenhadFileType& fileType) {
    QFile file (filename);
    QTextStream ds (&file);
    QDomDocument doc = serialize (fileType);

    std::cout.flush();
    if (! file.open (QIODevice::WriteOnly | QIODevice::Text )) {
        CalenhadServices::messages() -> message ("File error", "Failed to open file for writing", NotificationStyle::ErrorNotification);
    } else {
        ds << doc.toString();
        file.close();
        CalenhadServices::messages() -> message ("Complete", "Wrote file " + filename, NotificationStyle::InfoNotification);
    }
}

QDomDocument CalenhadModel::serialize (const CalenhadFileType& fileType) {
    QDomDocument doc;
    QDomElement root = doc.createElement ("calenhad");
    doc.appendChild (root);
    writeMetadata (doc);

    // Always include all variables in the file
    CalenhadServices::calculator() -> serialize (root);

    // Always include all known legends in the file
    QDomElement legendsElement = doc.createElement ("legends");
    root.appendChild (legendsElement);
    for (Legend* legend : CalenhadServices::legends() -> all()) {
        legend -> serialise (doc);
    }

    if (fileType == CalenhadFileType::CalenhadModelFile) {
        QDomElement modelElement = doc.createElement ("model");
        root.appendChild (modelElement);
        // serialize top-level nodes
        // each group found will serialize its contents recursively
        QDomElement nodesElement = doc.createElement ("nodes");
        modelElement.appendChild (nodesElement);
        for (Node* qm : nodes()) {
            if (! (qm -> group())) {
                qm -> serialize (nodesElement);
            }
        }

        // serialize connections
        QDomElement connectionsElement = doc.createElement ("connections");
        modelElement.appendChild (connectionsElement);
        for (Connection* c : connections()) {
            c -> serialise (connectionsElement);
        }
        _changed = false;
    }
    return doc;
}

void CalenhadModel::writeMetadata (QDomDocument& doc) {
    QDomElement metadata = doc.createElement ("metadata");
    doc.documentElement().appendChild (metadata);
    if (! _title.isEmpty()) {
        QDomElement titleElement = doc.createElement ("title");
        metadata.appendChild (titleElement);
        QDomText titleContent = doc.createTextNode (_title);
        titleElement.appendChild (titleContent);
    }

    QDomElement dateElement = doc.createElement ("date");
    metadata.appendChild (dateElement);
    _date = QDateTime::currentDateTime();
    QDomText dateContent = doc.createTextNode (_date.toString ("dd MMMM yyyy hh:mm"));
    dateElement.appendChild (dateContent);

    if (! _author.isEmpty()) {
        QDomElement authorElement = doc.createElement ("author");
        metadata.appendChild (authorElement);
        QDomText authorContent = doc.createTextNode (_author);
        authorElement.appendChild (authorContent);
    }
    if (! _description.isEmpty()) {
        QDomElement descriptionElement = doc.createElement ("description");
        metadata.appendChild (descriptionElement);
        QDomText descriptionContent = doc.createTextNode (_description);
        descriptionElement.appendChild (descriptionContent);
    }
}

void CalenhadModel::readMetadata (const QDomDocument& doc) {
    QDomElement metadataElement = doc.documentElement().firstChildElement ("metadata");
    QDomElement authorElement = metadataElement.firstChildElement ("author");
    _author = authorElement.isNull() ? "" : authorElement.text();
    QDomElement titleElement = metadataElement.firstChildElement ("title");
    _title = titleElement.isNull() ? _filename : titleElement.text();
    if (_title.isNull() || _title.isEmpty()) { _title = _filename; }
    emit titleChanged (_title);
    QDomElement descriptionElement = metadataElement.firstChildElement ("description");
    _description = descriptionElement.isNull() ? "" : descriptionElement.text();
    QDomElement dateElement = metadataElement.firstChildElement ("date");
    _date = dateElement.isNull() ? QDateTime::currentDateTime() : QDateTime::fromString (dateElement.nodeValue(), "dd MMMM yyyy hh:mm");
}


void CalenhadModel::inflate (const QString& filename, const CalenhadFileType& fileType) {
    QDomDocument doc;
    if (CalenhadServices::readXml (filename, doc)) {
        readMetadata (doc);
        inflate (doc, fileType);
    }
    _filename = filename;
}


void CalenhadModel::inflate (const QDomDocument& doc, const CalenhadFileType& fileType) {

    // Always retrieve all legends from the file
    QDomNodeList legendNodes = doc.documentElement().elementsByTagName ("legend");
    for (int i = 0; i < legendNodes.size(); i++) {
        Legend* legend = new Legend();
        legend -> inflate (legendNodes.at (i));
        CalenhadServices::legends() -> provide (legend);
    }

    QDomNodeList conns = doc.documentElement ().firstChildElement ("fragment").firstChildElement ("connections").childNodes ();

    // if we are pasting, deduplicate node names and update connections to point to correct nodes
    if (fileType == CalenhadFileType::CalenhadModelFragment) {
        QDomElement element = doc.documentElement ().firstChildElement ("fragment").firstChildElement ("nodes");
        QDomNodeList modules = element.childNodes ();
        for (int i = 0; i < modules.size (); i++) {
            QDomElement e = modules.at (i).toElement ();
            QDomElement nameElement = e.firstChildElement ("name");
            QString original = nameElement.text ();

            if (findModule (original)) {
                QString newName = uniqueName (original);
                e.setAttribute ("name", newName);
                for (int j = 0; j < conns.size (); j++) {
                    QDomElement c = conns.at (j).toElement ();
                    QDomElement from = c.firstChildElement ("source");
                    QDomElement to = c.firstChildElement ("target");
                    if (from.attribute ("module") == original) { from.setAttribute ("module", newName); }
                    if (to.attribute ("module") == original) { to.setAttribute ("module", newName); }
                }
            }
        }

        // clear the selection, so that we can select the pasted items instead
        foreach (QGraphicsItem* item, items()) {
            item -> setSelected (false);
        }

        inflate (element, fileType);
        inflateConnections (conns);
    }

    if (fileType == CalenhadFileType::CalenhadModelFile) {

        // inflate variables for the calculator
        QDomElement variablesElement = doc.documentElement ().firstChildElement ("variables");
        CalenhadServices::calculator() -> inflate (variablesElement);
        QDomElement element = doc.documentElement().firstChildElement ("model").firstChildElement ("nodes");
        inflate (element, fileType);
        inflateConnections (doc, fileType);
    }
}

void CalenhadModel::inflateConnections (const QDomDocument& doc, const CalenhadFileType& fileType) {

    // In the connections, we save and retrieve the types of output ports in case we ever have further types of output ports.
    // Does not support a port serving as both input and output (because index presently not unique across both).
    // For the time being however all output ports will be of type 2 (Port::Output).

    QDomNodeList connectionNodes = doc.documentElement ().firstChildElement ("model").firstChildElement ("connections").elementsByTagName ("connection");
    inflateConnections (connectionNodes);
}

void CalenhadModel::inflateConnections (QDomNodeList& connectionNodes) {
    for (int i = 0; i < connectionNodes.count (); i++) {
        QDomElement fromElement = connectionNodes.at (i).firstChildElement ("source");
        QDomElement toElement = connectionNodes.at (i).firstChildElement ("target");
        Module* fromNode = findModule (fromElement.attributes ().namedItem ("module").nodeValue ());
        Module* toNode = findModule (toElement.attributes ().namedItem ("module").nodeValue ());
        if (fromNode && toNode) {
            Port* fromPort = nullptr, * toPort = nullptr;
            for (Port* port : fromNode->ports ()) {
                bool okIndex;
                int index = fromElement.attribute ("output").toInt (&okIndex);
                if (okIndex) {
                    if (port->index () == index && port->portType () == Port::OutputPort) {
                        fromPort = port;
                        fromPort->setName (fromElement.attribute ("name"));
                    }
                }
            }
            for (Port* port : toNode->ports ()) {
                bool okIndex;
                int index = toElement.attribute ("input").toInt (&okIndex);
                if (okIndex) {
                    if (port->index () == index && port->portType () != Port::OutputPort) {
                        toPort = port;
                        toPort->setName (fromElement.attribute ("name"));
                    }
                }
            }
            if (fromPort && toPort) {
                doConnectPorts (fromPort, toPort);
            } else {
                if (!fromPort) { CalenhadServices::messages ()->message ("Couldn't connect source", "No output port available for source", NotificationStyle::ErrorNotification); }
                if (!toPort) { CalenhadServices::messages ()->message ("Couldn't connect target", "No input port available for target", NotificationStyle::ErrorNotification); }
            }
        }
    }
}

void CalenhadModel::inflate (const QDomElement& parent, const CalenhadFileType& fileType) {

    if (fileType == CalenhadFileType::CalenhadModelFile || fileType == CalenhadFileType::CalenhadModelFragment) {

        // inflate modules group by group
        QDomNode n = parent.firstChild();
        while (! n.isNull()) {
            QDomElement element = n.toElement();
            QString type = n.attributes ().namedItem ("type").nodeValue ();
            QDomNodeList connectionNodes = element.ownerDocument().documentElement().firstChildElement ("connections").elementsByTagName ("connection");
            // put the node at the requested position on the canvas
            QDomElement positionElement = n.firstChildElement ("position");
            double x = positionElement.attributes ().namedItem ("x").nodeValue ().toDouble();
            double y = positionElement.attributes ().namedItem ("y").nodeValue ().toDouble();
            QPointF pos (x, y);

            // if we are pasting, offset the positions so that we can see copied and pasted items separately
            if (fileType == CalenhadFileType::CalenhadModelFragment) {
                pos.setX (pos.x() + CalenhadServices::preferences ()->calenhad_module_duplicate_offset_x);
                pos.setY (pos.y() + CalenhadServices::preferences ()->calenhad_module_duplicate_offset_y);
            }


            QDomElement nameNode = n.firstChildElement ("name");
            QString name = nameNode.text ();
            QString newName = uniqueName (name);
            std::cout << name.toStdString () << " - New name: " << newName.toStdString () << "\n";
            // if node is a group, add its contents recursively
            if (type == "nodegroup") {
                NodeGroup* ng = addNodeGroup (pos, newName);
                ng -> inflate (n.toElement());
                // if nodegroup is in another group, assign the group
                QDomElement gp = n.parentNode().parentNode().toElement();
                NodeGroupBlock* block = (NodeGroupBlock*) ng -> handle();


                // restore the nodegroup's size
                bool ok;
                double height = element.attribute ("height").toDouble (&ok);
                double width = ok ? element.attribute ("width").toDouble (&ok) : 0.0;

                if (ok) {
                    block -> resize (QRectF (pos.x(), pos.y(), width, height));
                }

                // restore the nodegroup's position
                if (gp.attribute ("type") == "nodegroup") {
                    NodeGroup* group = findGroup (gp.firstChildElement ("name").text ());
                    if (group) {
                        ng -> setGroup (group);
                        block -> setParentItem (group -> handle());
                        block -> setPos (pos.x(), pos.y());
                    }
                }

                QDomElement nodesElement = n.firstChildElement ("nodes");
                inflate (nodesElement, fileType);

            } else {
                Module* qm = addModule (pos, type, newName);
                qm -> handle() -> setSelected (fileType == CalenhadFileType::CalenhadModelFragment);
                qm -> inflate (n.toElement());

                // if module is in a group, assign the group
                QDomElement gp = n.parentNode().parentNode().toElement();
                if (gp.attribute ("type") == "nodegroup") {
                    NodeGroup* group = findGroup (gp.firstChildElement ("name").text());
                    if (group) {
                        qm -> setGroup (group);
                        qm -> handle() -> setParentItem (group -> handle ());
                        qm -> handle() -> setPos (pos);
                    }
                }

                // update connection names so that the module is still found if it was renamed

                for (int i = 0; i < connectionNodes.count (); i++) {
                    QDomElement fromElement = connectionNodes.at (i).firstChildElement ("source");
                    QDomElement toElement = connectionNodes.at (i).firstChildElement ("target");
                    if (fromElement.attribute ("module") == name) {
                        fromElement.setAttribute ("module", newName);
                    }
                    if (toElement.attribute ("module") == name) {
                        toElement.setAttribute ("module", newName);
                    }
                }

                _changed = false;
            }
            n = n.nextSibling ();
        }
    }
}

void CalenhadModel::highlightGroupAt (QPointF pos) {
    for (NodeGroup* group : nodeGroups()) {
        ((NodeGroupBlock*) group -> handle()) -> setHighlight (false);
    }

    qreal top = -1000;
    QList<QGraphicsItem*> items = QGraphicsScene::items (pos);
    NodeGroupBlock* target = nullptr;
    for (QGraphicsItem* item : items) {
        NodeGroupBlock* block = dynamic_cast<NodeGroupBlock*> (item);
        if (block) {
            if (block->zValue () >= top) {
                target = block;
                target -> setHighlight (true);
                top = target->zValue ();
            }
        }
    }
}

void CalenhadModel::commitLegends() {
    QString file = CalenhadServices::preferences() -> calenhad_legends_filename_temp;
    serialize (file, CalenhadFileType::CalenhadLegendFile);
    setChanged();
}

void CalenhadModel::rollbackLegends() {
    CalenhadServices::legends() -> clear();

    // Load legends from temp legends file
    QString file = CalenhadServices::preferences() -> calenhad_legends_filename_temp;
    inflate (file, CalenhadFileType::CalenhadLegendFile);
}

QMenu* CalenhadModel::makeMenu (QGraphicsItem* item) {
    if (_menu) { delete _menu; _menu = nullptr; }
    _menu = nullptr;
    // construct menu for whatever item type here because QGraphicsItem does not extend QObject, so we can't call connect within QGraphicsItem
    if (dynamic_cast<Port*> (item)) {
        Port* port = static_cast<Port*> (item);
        _menu = new QMenu ("Port");
        _menu -> addMenu (port -> connectMenu());
        return _menu;
    }
    if (dynamic_cast<Connection*> (item)) {
        // connection actions
        _menu = new QMenu ("Connection");
        Connection* c = static_cast<Connection*> (item);
        _menu -> addAction (makeMenuItem (QIcon (":/appicons/controls/disconnect.png"), "Disconnect", "Delete this connection from the model", CalenhadAction::DeleteConnectionAction, c));
    }
    if (dynamic_cast<NodeBlock*> (item)) {
        NodeBlock* block = static_cast<NodeBlock*> (item);
        Node* n = block -> node();
        _menu = new QMenu (n -> name() + " (" + n -> nodeType() + ")");
        _menu -> addAction (makeMenuItem (QIcon (":/appicons/controls/duplicate.png"), tr ("Duplicate module"), "Duplicate module", CalenhadAction::DuplicateModuleAction, block));
        _menu -> addAction (makeMenuItem (QIcon (":/appicons/controls/delete.png"), tr ("Delete module"), "Delete module", CalenhadAction::DeleteModuleAction, block));

        _menu -> addSeparator();

        QAction* editAction = new QAction (QIcon (":/appicons/controls/edit.png"), tr ("Edit"));
        editAction -> setToolTip ("Edit module's details and parameters");
        connect (editAction, &QAction::triggered, this, [=]() { n->showModuleDetail (true); });
        _menu -> addAction (editAction);

        if (dynamic_cast<Module*> (n)) {
            QAction* globeAction = new QAction (QIcon (":/appicons/controls/globe.png"), "Show globe");
            connect (globeAction, &QAction::triggered, (Module*) n, &Module::showGlobe);
            _menu->addAction (globeAction);
        }
    }

    if (dynamic_cast<NodeGroupBlock*> (item)) {
        NodeGroupBlock* block = static_cast<NodeGroupBlock*> (item);
        NodeGroup* group = (NodeGroup*) block -> node();
        _menu = new QMenu (group -> name() + " (Node group)");
    }

    if (! _menu) {
        _menu = new QMenu ("Model");
    }

    // actions that operate on selections
    _menu -> addSeparator();
    QAction* copy = makeMenuItem (QIcon (":/appicons/controls/copy.png"), tr ("Copy selection"), "Copy selection", CalenhadAction::CopyAction, nullptr);
    _menu -> addAction (copy);
    QAction* cut = makeMenuItem (QIcon (":/appicons/controls/cut.png"), tr ("Cut selection"), "Cut selection", CalenhadAction::CutAction, nullptr);
    _menu -> addAction (cut);
    QAction* deleteSelection = makeMenuItem (QIcon (":/appicons/controls/delete_selection.png"), tr ("Delete selection"), "Delete selection", CalenhadAction::DeleteSelectionAction, nullptr);
    _menu -> addAction (deleteSelection);
    QAction* newGroupFromSelection = makeMenuItem (QIcon (":/appicons/controls/group_add.png"), tr ("New group from selection"), "New group from selection", CalenhadAction::NodeGroupFromSelectionAction, nullptr);
    _menu -> addAction (newGroupFromSelection);
    _menu -> addSeparator();
    QAction* paste = makeMenuItem (QIcon (":/appicons.controls.paste.png"), tr ("Paste"), "Paste", CalenhadAction::PasteAction, nullptr);
    _menu -> addAction (paste);
    copy -> setEnabled (selectedItems().size() > 0);
    cut -> setEnabled (selectedItems().size() > 0);
    deleteSelection -> setEnabled (selectedItems().size() > 0);
    paste -> setEnabled (QGuiApplication::clipboard() -> text() != "");
    newGroupFromSelection -> setEnabled (selectedItems().size() > 0);

    // actions that operate on the canvas
    // ...


    return _menu;
}

QAction* CalenhadModel::makeMenuItem (const QIcon& icon, const QString& name, const QString& statusTip, const QVariant& id, QGraphicsItem* item) {
    ContextAction<QGraphicsItem>* action = new ContextAction<QGraphicsItem> (item, icon, name, this);
    action -> setStatusTip (statusTip);
    action -> setCheckable (false);
    action -> setData (id);
    connect (action, &QAction::triggered, _controller, &CalenhadController::actionTriggered);
    return action;
}

QString CalenhadModel::uniqueName (QString original) {
    int i = 0;
    QString suffix;
    while (nameExists (original + suffix)) {
        suffix = "_" + QString::number (i++);
    } ;
    return (original + suffix);
}

bool CalenhadModel::isChanged() {
    return _changed;
}

const QString& CalenhadModel::title() {
    return _title;
}

const QString& CalenhadModel::author() {
    return _author;
}

const QDateTime& CalenhadModel::lastSave() {
    return _lastSaved;
}

const QString& CalenhadModel::filename() {
    return _filename;
}

const QString& CalenhadModel::description() {
    return _description;
}

void CalenhadModel::setTitle (const QString& title) {
    _title = title;
}

void CalenhadModel::setAuthor (const QString& author) {
    _author = author;
}

void CalenhadModel::setDescription (const QString& description) {
    _description = description;
}

void CalenhadModel::preserve() {
     if (_undoEnabled) {
         QDomDocument doc = serialize (CalenhadFileType::CalenhadModelFile);
         _oldXml = doc.toString ();
     }

}

void CalenhadModel::removeAll() {
    for (Node* n : nodes()) {
        doDeleteNode (n);
    }
    clear();
}

QString CalenhadModel::snapshot () {
    QDomDocument doc = serialize (CalenhadFileType::CalenhadModelFile);
    QString newXml = doc.toString();
    return newXml;
}

QString CalenhadModel::lastSnapshot () {
    return _oldXml;
}

void CalenhadModel::restore (const QString& xml) {
    if (! xml.isNull()) {
        setUndoEnabled (false);
        removeAll();
        QDomDocument doc;
        doc.setContent (xml);
        inflate (doc);
        setUndoEnabled (true);
    }
}

void CalenhadModel::setRestorePoint (const QString& text) {
    if (_undoEnabled) {
        QString old = lastSnapshot ();
        XmlCommand* command = new XmlCommand (this, old, QString::null);
        command -> setText (text);
        _controller -> doCommand (command);
        QString newXml = snapshot ();
        command -> setNewXml (newXml);
    }
}

void CalenhadModel::setChanged (const bool& changed) {
    _changed = changed;

    // make sure the scene canvas entirely contains all the nodes on it - otherwise they end up half off the edge when we scroll
    CalenhadView* view =(CalenhadView*)  views().at (0);
    QPointF centre = view -> sceneRect().center();
    //setSceneRect (itemsBoundingRect ());
    view -> centerOn (centre);

}

void CalenhadModel::setUndoEnabled (const bool& enabled) {
    _undoEnabled = enabled;
}

void CalenhadModel::createConnection (Port* from, Port* to) {
    if (from && to) {
        preserve();
        doConnectPorts (from, to);
        if (conn) {
            delete conn;
            conn = nullptr;
        }
        update();
        setChanged (true);
        setRestorePoint();
    }
}

void CalenhadModel::doDuplicateNode (calenhad::qmodule::Node* node) {
    preserve();
    Node* copy = node -> clone();
    QPointF p (node -> handle() -> pos());
    p.setX (p.x() + CalenhadServices::preferences() -> calenhad_module_duplicate_offset_x);
    p.setY (p.y() + CalenhadServices::preferences() -> calenhad_module_duplicate_offset_y);
    copy -> setName (uniqueName (node -> name()));
    addNode (copy, p);
    setChanged (true);
    setRestorePoint();
}

QString CalenhadModel::selectionToXml() {
    QDomDocument doc;
    QDomElement root = doc.createElement ("calenhad");
    doc.appendChild (root);
    QDomElement fragment = doc.createElement ("fragment");
    root.appendChild (fragment);
    QDomElement nodes = doc.createElement ("nodes");
    QDomElement conns = doc.createElement ("connections");
    fragment.appendChild (nodes);
    fragment.appendChild (conns);
    // copy any connections that are between nodes that are both in the selection
    for (Connection* c : connections ()) {
        Node* n0 = c->port1 ()->owner ();
        Node* n1 = c->port2 ()->owner ();
        if (n1->handle ()->isSelected () && n0->handle ()->isSelected ()) {
            c->setSelected (true);
        }
    }

    // serialise all selected items to the document
    for (QGraphicsItem* item : selectedItems ()) {
        NodeBlock* b = dynamic_cast<NodeBlock*> (item);
        if (b) {
            Node* n = b->node ();
            n->serialize (nodes);
        }
        Connection* c = dynamic_cast<Connection*> (item);
        if (c) {
            c->serialise (conns);
        }
    }

    return doc.toString ();

}