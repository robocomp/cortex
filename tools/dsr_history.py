import sys

from PySide2.QtCore import QObject, QTimer, QElapsedTimer, Signal, QSettings, QLineF, QPointF
from PySide2.QtGui import Qt, QBrush, QPainter, QRadialGradient, QGradient, QColor, QPen, QPalette, QTransform
from PySide2.QtWidgets import QWidget, QMenu, QMainWindow, QApplication, QAction, QDockWidget, \
    QFileDialog, QPushButton, QHBoxLayout, QLabel, QSlider, QGraphicsScene, \
    QGraphicsLineItem, QGraphicsEllipseItem, QGraphicsView, QGraphicsSimpleTextItem, QStyle, QToolBar, QLabel, \
    QScrollArea, QVBoxLayout, QSizePolicy, QFrame, QTableWidget, QTableWidgetItem, QGraphicsItem

from GHistoryState import GHistoryState, get_file
from pydsr import Node, Edge, Attribute

import difflib


NODE_COLORS = {
    "root": "red",
    "transform" : "SteelBlue",
    "room" : "Gray",
    "differentialrobot" : "GoldenRod",
    "omnirobot" : "Gray",
    "robot" : "Gray",
    "path_to_target" : "Gray",
    "intention" : "Gray",
    "rgbd" : "Gray",
    "pan_tilt" : "Gray",
    "battery" : "Gray",
    "pose" : "Gray",
    "laser" : "GreenYellow",
    "camera" : "Gray",
    "imu" : "LightSalmon",
    "slam_device" : "Gray",
    "object" : "Gray",
    "affordance_space" : "Gray",
    "person" : "Gray",
    "personal_space" : "Gray",
    "plane" : "Khaki",
    "box" : "Gray",
    "cylinder" : "Gray",
    "ball" : "Gray",
    "mesh" : "LightBlue",
    "face" : "Gray",
    "body" : "Gray",
    "chest" : "Gray",
    "nose" : "Gray",
    "left_eye" : "Gray",
    "right_eye" : "Gray",
    "left_ear" : "Gray",
    "right_ear" : "Gray",
    "left_arm" : "Gray",
    "right_arm" : "Gray",
    "left_shoulder" : "Gray",
    "right_shoulder" : "Gray",
    "left_elbow" : "Gray",
    "right_elbow" : "Gray",
    "left_wrist" : "Gray",
    "right_wrist" : "Gray",
    "left_hand" : "Gray",
    "right_hand" : "Gray",
    "left_hip" : "Gray",
    "right_hip" : "Gray",
    "left_leg" : "Gray",
    "right_leg" : "Gray",
    "left_knee" : "Gray",
    "right_knee" : "Gray",
    "mug" : "Gray",
    "cup" : "Gray",
    "noodles" : "Gray",
    "table" : "Gray",
    "chair" : "Gray",
    "shelve" : "Gray",
    "dish" : "Gray",
    "spoon" : "Gray",
    "testtype" : "Gray",
    "glass" : "Gray",
    "plant" : "Gray",
    "microwave" : "Gray",
    "oven" : "Gray",
    "vase" : "Gray",
    "refrigerator" : "Gray",
    "road" : "Gray",
    "building" : "Gray",
    "vehicle" : "Gray",
    "gps" : "Gray",
    "grid" : "Gray",
    "agent" : "Gray"
}


class GraphNodeWidget(QTableWidget):

    def __init__(self, node_or_edge):
        QTableWidget.__init(self)

        if isinstance(node_or_edge, Node):
            self.name = f"Node: {Obj.id}, {Obj.type}, {Obj.name}"
        else:
            self.name = f"Edge: {Obj.origin}, {Obj.destination}, {Obj.type}"

        self.setWindowTitle(f"{self.name} ")

        for k, v in node_or_edge.atts.items():
            self.__insert_att(k, v)

        self.horizontalHeader().setStretchLastSection(True)
        self.resize_widget()

        self.show()
    def __insert_att(self, key, value):
        rc = self.rowCount()
        self.insertRow(rc)
        val_str = str(value.value)
        if len(val_str) > 400:
            val_str= val_str[:400] + " ... "
        item = QTableWidgetItem(val_str)
        item.setFlags(Qt.ItemIsSelectable| Qt.ItemIsEnabled)
        self.setItem(rc, 0, item)

class GraphNode(QObject, QGraphicsEllipseItem):

    def __init__(self, node):
        QObject.__init__(self)
        QGraphicsEllipseItem.__init__(self, 0, 0, 20, 20)
        self.node_brush = QBrush()
        self.node = node
        self.setAcceptHoverEvents(True)
        self.setZValue(-1)
        #self.node_brush.setStyle(Qt.SolidPattern)
        #self.node_brush.setStyle(Qt.SolidPattern)
        #self.setCacheMode(QGraphicsItem.CacheMode.DeviceCoordinateCache)
        self.setFlags(QGraphicsItem.GraphicsItemFlag.ItemIsMovable | QGraphicsItem.GraphicsItemFlag.ItemIsSelectable | \
                      QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges | \
                      QGraphicsItem.GraphicsItemFlag.ItemUsesExtendedStyleOption | \
                      QGraphicsItem.GraphicsItemFlag.ItemIsFocusable)

    def setTag(self, tag):
        self.tag = QGraphicsSimpleTextItem(tag, self)
        self.tag.setX(20)
        self.tag.setY(-10)

    def __setColor(self, color):
        self.node_brush.setColor(color)
        self.setBrush(self.node_brush)

    def setType(self, type):
        #TODO: Add click to show tables.
        color = NODE_COLORS[type] if NODE_COLORS.get(type) else "coral"
        self.__setColor(color)
        pass

    def mouseDoubleClickEvent(self, event):
        super().hoverEnterEvent(event)
        print("doble click")
        if event.button() == Qt.RightButton:
            self.node_widget = GraphNodeWidget(self.node)

        QGraphicsEllipseItem.mouseDoubleClickEvent(event)

    def paint(self, painter, option, widget):
        painter.setPen(Qt.NoPen)
        #painter.setBrush(self.node_brush)

        gradient = QRadialGradient(-3, -3, 20)
        #if option.state &  QStyle.State_Sunken:
        #    gradient.setColorAt(0, QColor(Qt.darkGray).light(200))
        #    gradient.setColorAt(1, QColor(Qt.darkGray))
        #else:

        gradient.setColorAt(0, self.node_brush.color())
        gradient.setColorAt(1, QColor(self.node_brush.color()).light(200))

        painter.setBrush(gradient)
        if self.isSelected():
            painter.setPen(QPen(Qt.green, 0, Qt.DashLine))
        else:
            painter.setPen(QPen(Qt.black, 0))

        painter.drawEllipse(-10, -10, 20, 20)

class GraphEdge(QObject, QGraphicsLineItem):

    def __init__(self, orig, dst, type):
        QObject.__init__(self)
        QGraphicsLineItem.__init__(self)
        self.color = "black"
        self.line_width = 2
        self.orig = orig
        self.dst = dst
        self.__setTag(type)
        self.__adjust()

    def __adjust(self):
        line  = QLineF(self.mapFromItem(self.orig, 0, 0), self.mapFromItem(self.dst, 0, 0))
        len = line.length()

        self.prepareGeometryChange()
        self.tag.setPos(line.center())
        if len > 20.0:
            edgeOffset = QPointF((line.dx() * 10) / len, (line.dy() * 10) / len)
            sourcePoint = QPointF(line.p1() + edgeOffset)
            destPoint = QPointF(line.p2() - edgeOffset)
            self.setLine(QLineF(sourcePoint, destPoint))
        else:
            self.setLine(line)

    def __setTag(self, tag):
        self.tag = QGraphicsSimpleTextItem(tag, self)


class Graph(QGraphicsView):

    def __init__(self):
        super(Graph, self).__init__()
        self.scene = QGraphicsScene()
        self.scene.setSceneRect(self.scene.itemsBoundingRect())
        self.fitInView(self.scene.itemsBoundingRect(), Qt.KeepAspectRatio)
        self.nodes = {}
        self.edges = {}
        self.visual_nodes = {}
        self.visual_edges = {}
        central_point = QGraphicsEllipseItem(0,0,0,0)
        self.scene.addItem(central_point)
        self.setScene(self.scene)
        self.setCacheMode(QGraphicsView.CacheBackground)
        self.setViewportUpdateMode(QGraphicsView.BoundingRectViewportUpdate)
        self.setRenderHint(QPainter.Antialiasing)
        self.setMouseTracking(True)
        self.viewport().setMouseTracking(True)


    def mousePressEvent(self, event):
        item = self.scene.itemAt(self.mapToScene(event.pos()), QTransform())
        if item:
            QGraphicsView.mousePressEvent(event)
        #else:
        #    AbstractGraphicViewer.mousePressEvent(event)

    def __add_or_replace_node(self, Node):

        gnode = None
        if not self.nodes.get(Node.id):
            gnode = GraphNode(Node)
            gnode.id_in_graph = Node.id
            gnode.setTag(Node.name)
            gnode.setType(Node.type)

            self.nodes[Node.id] = Node
            self.visual_nodes[Node.id] = gnode
            self.scene.addItem(gnode)
            #print("New node", Node.name, Node.id)
        else:
            gnode = self.visual_nodes[Node.id]
            gnode.node = Node
            self.nodes[Node.id] = Node
            #print("Existing node", Node.name, Node.id)

        gnode.setPos(Node.attrs["pos_x"].value, Node.attrs["pos_y"].value)


    def __add_or_replace_edge(self, Edge):

        gedge = None
        key = (Edge.origin, Edge.destination, Edge.type)
        if not self.visual_nodes.get(Edge.origin) or not self.visual_nodes.get(Edge.destination):
            return

        if not self.edges.get(key):
            gedge = GraphEdge(self.visual_nodes[Edge.origin], self.visual_nodes[Edge.destination], Edge.type)

            self.edges[key] = Edge
            self.visual_edges[key] = gedge
            self.scene.addItem(gedge)
        else:
            gedge = self.visual_edges[key]
            self.edges[key] = Edge

    def set_state(self, nodes, edges):
        #TODO det diff in keys to remove nodes.
        old_node_keys = set(self.nodes.keys())
        ns = set([n[1].id for n in nodes if n[1]])
        node_keys_to_remove = [n for n in old_node_keys if n not in ns]

        old_edge_keys = set(self.edges.keys())
        es = set([(Edge[1].origin, Edge[1].destination, Edge[1].type) for Edge in edges if Edge[1]])
        edge_keys_to_remove = [e for e in old_edge_keys if e not in es]



        for id in node_keys_to_remove:
            del self.nodes[id]
            self.scene.removeItem(self.visual_nodes[id])
            del self.visual_nodes[id]

        for e_key in edge_keys_to_remove:
            del self.edges[e_key]
            self.scene.removeItem(self.visual_edges[e_key])
            del self.visual_edges[e_key]

        for n in nodes:
            if n[1] is not None:
                self.__add_or_replace_node(n[1])


        for e in edges:
            if e[1] is not None:
                self.__add_or_replace_edge(e[1])


class DSRHistoryViewer(QObject):

    def __init__(self, window,):
        super().__init__()
        self.timer = QTimer()
        self.alive_timer = QElapsedTimer()
        self.gh = None
        self.slider = None
        self.window = window
        self.view_menu = QMenu()
        self.file_menu = QMenu()
        self.forces_menu = QMenu()
        self.main_widget = window
        self.docks = {}
        self.widgets = {}
        self.widgets_by_type = {}

        available_geometry = QApplication.desktop().availableGeometry()
        window.move((available_geometry.width() - window.width()) / 2,
                    (available_geometry.height() - window.height()) / 2)
        self.__initialize_file_menu()


    def __del__(self):
        settings = QSettings("RoboComp", "DSR")
        settings.beginGroup("MainWindow")
        settings.setValue("size", self.window.size())
        settings.setValue("pos", self.window.pos())
        settings.endGroup()


    def __initialize_file_menu(self):
        file_menu = self.window.menuBar().addMenu(self.window.tr("&File"))
        load_action = QAction("Load", self)
        file_menu.addAction(load_action)
        # load_action
        load_action.triggered.connect(lambda: self.__load_file())


    def __load_file(self):
        file_name = QFileDialog.getOpenFileName(None, 'Select file')
        print("------------------\n", str(file_name))
        val = get_file(file_name[0])
        self.gh = GHistoryState(val)
        self.__set_state(0)
        self.__create_slider()
        self.__load_main_widget()
        nodes, edges = self.gh.get_state(0)
        self.graphview.set_state(nodes, edges)
        self.slider.valueChanged.connect(self.__change_slider_value)
        self.__create_right_dock()

        print("File loaded")

    def __set_state(self, idx : int):
        pass

    def __change_slider_value(self):
        val = self.slider.value()
        self.label.setText(str(val))
        nodes, edges = self.gh.get_state(val)
        self.graphview.set_state(nodes, edges)

        (old, new) = self.gh.get_change_state(self.slider.value())

        if hasattr(self, "difflines"):
            for idx, i in enumerate(self.difflines):
                self.container_widget.layout().removeWidget(i)
                i.deleteLater()
            self.difflines = []

        def set_labels(self, Obj):
            if isinstance(Obj, Node):
                self.toolbar2_change.setText(f"Change Node: {Obj.id}, {Obj.type}, {Obj.name}")
            elif isinstance(Obj, Edge):
                self.toolbar2_change.setText(f"Change Edge: {Obj.origin}, {Obj.destination}, {Obj.type}")
            else:
                self.newLabel.setText("Change")

            #print(self.toolbar2_change.text())


        if not old and new:
            set_labels(self, new)
            self.oldLabel.setStyleSheet("QLabel { background-color : red;}")
            self.newLabel.setStyleSheet("QLabel { background-color : green; }")
            self.oldLabel.setText("")
            self.newLabel.setText(str(new))
        elif old and not new:
            set_labels(self, old)
            self.oldLabel.setStyleSheet("QLabel { background-color : red;}")
            self.newLabel.setStyleSheet("QLabel { background-color : green;}")
            self.oldLabel.setText(str(old))
            self.newLabel.setText("")
        elif old and new:
            diff = difflib.unified_diff(str(old).split("\n"), str(new).split("\n"), lineterm='', n=-1)

            if not hasattr(self, "difflines"):
                self.difflines = []

            diff = list(diff)
            #print('\n'.join(diff), end="")

            self.oldLabel.setStyleSheet("QLabel {background-color : red;}")
            self.newLabel.setStyleSheet("QLabel {background-color : green;}")
            self.oldLabel.setText("")
            self.newLabel.setText("")

            for l in diff[3:]:
                pl = QLabel(l)
                pl.setWordWrap(True)
                pl.setMinimumWidth(0)
                pl.setMaximumWidth(420)

                if l.startswith("+"):
                    pl.setStyleSheet("QLabel {  background-color : green; }")
                elif l.startswith("-"):
                    pl.setStyleSheet("QLabel {  background-color : red; }")
                elif l.startswith("@"):
                    continue
                self.container_widget.layout().addWidget(pl)
                self.difflines.append(pl)

            set_labels(self, new)

            #self.oldLabel.setText(str(old))
            #self.newLabel.setText(str(new))

        else:
            self.oldLabel.setStyleSheet("QLabel { background-color : red;}")
            self.newLabel.setStyleSheet("QLabel { background-color : blue;}")
            self.oldLabel.setText("")
            self.newLabel.setText("")



    def __create_slider(self):
        self.toolbar = self.window.addToolBar('ToolBar')
        self.toolbar.setMinimumHeight(75)
        self.toolbar.setMovable( False )
        self.slider = QSlider(Qt.Horizontal)
        self.slider.setOrientation(Qt.Horizontal)
        self.slider.setTickInterval(1)
        self.slider.setMinimum(0)
        self.slider.setMaximum(self.gh.states - 1)
        self.label = QLabel("0")

        self.toolbar.addWidget(self.slider)
        self.toolbar.addWidget(self.label)

    def __create_right_dock(self):

        self.change_d = QDockWidget()
        self.change_d.setAllowedAreas(Qt.AllDockWidgetAreas)
        self.change_d.setMinimumWidth(450)

        self.sa = QScrollArea()
        self.sa.setBackgroundRole(QPalette.Window)
        self.sa.setFrameShadow(QFrame.Plain)
        self.sa.setFrameShape(QFrame.NoFrame)
        self.sa.setWidgetResizable(True)

        self.container_widget = QWidget(self.sa)
        self.container_widget.setSizePolicy(QSizePolicy.MinimumExpanding, QSizePolicy.MinimumExpanding)
        self.container_widget.setLayout(QVBoxLayout(self.container_widget))
        self.sa.setWidget(self.container_widget)
        self.container_widget.layout().setAlignment(Qt.AlignTop)

        self.oldLabel = QLabel("")
        self.newLabel = QLabel("")
        self.oldLabel.setStyleSheet("QLabel { background-color : red; }")
        self.newLabel.setStyleSheet("QLabel { background-color : green; }")
        self.newLabel.setWordWrap(True)
        self.oldLabel.setWordWrap(True)

        self.toolbar2_change = QLabel("Change")

        self.toolbar2_change.setWordWrap(True)
        self.toolbar2_change.setMaximumWidth(420)
        self.oldLabel.setMaximumWidth(420)
        self.newLabel.setMaximumWidth(420)

        self.container_widget.layout().addWidget(self.toolbar2_change)
        self.container_widget.layout().addWidget(self.oldLabel)
        self.container_widget.layout().addWidget(self.newLabel)
        #self.toolbar2.addWidget(self.sa)
        #self.window.addToolBar(Qt.RightToolBarArea, self.toolbar2)

        self.change_d.setWidget(self.sa)
        self.window.addDockWidget(Qt.RightDockWidgetArea, self.change_d)

    def __load_main_widget(self):
        self.graphview_d = QDockWidget()
        self.graphview_d.setAllowedAreas(Qt.AllDockWidgetAreas)
        self.graphview = Graph()
        self.graphview_d.setWidget(self.graphview)
        self.window.addDockWidget(Qt.LeftDockWidgetArea, self.graphview_d)
        #self.window.setCentralWidget(self.graphview)

if __name__ == '__main__':
    #sys.path.append('/opt/robocomp/lib')
    app = QApplication(sys.argv)
    from pydsr import *

    main_window = QMainWindow()
    main_window.resize(1280, 720)
    ui = DSRHistoryViewer(main_window)
    main_window.show()
    app.exec_()
