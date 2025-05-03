import os
import sys 
sys.path.insert(0, '~/.local/lib/python3.10/site-packages/PySide6/Qt/lib/')

from PySide6 import QtWidgets
from PySide6.QtWidgets import QApplication
from PySide6 import QtWidgets, QtCore

from rich.console import Console

from pydsr import *

#SIGNALS handler
def sigint_handler(*args):
    QtCore.QCoreApplication.quit()


ETC_DIR = "../etc/"
g = DSRGraph(int(0), "Test", int(12), os.path.join(ETC_DIR, "autonomyLab_objects.simscene.json"), True)
console = Console(highlight=False)

# =============== Emulate Agent Architecture ============
# =======================================================

class GenericWorker(QtWidgets.QWidget):
    def __init__(self, mprx):
        super(GenericWorker, self).__init__()
    

class SpecificWorker(GenericWorker):
    def __init__(self, proxy_map, startup_check=False):
        super(SpecificWorker, self).__init__(proxy_map)
    
    def update_node_att(self, id: int, attribute_names: [str]):
        console.print(f"UPDATE NODE ATT: {id} {attribute_names}", style='green')

    def update_node(self, id: int, type: str):
        console.print(f"UPDATE NODE: {id} {type}", style='green')

    def delete_node(self, id: int):
        console.print(f"DELETE NODE:: {id} ", style='green')

    def update_edge(self, fr: int, to: int, type: str):

        console.print(f"UPDATE EDGE: {fr} to {type}", type, style='green')

    def update_edge_att(self, fr: int, to: int, type: str, attribute_names: [str]):
        console.print(f"UPDATE EDGE ATT: {fr} to {type} {attribute_names}", style='green')

    def delete_edge(self, fr: int, to: int, type: str):
        console.print(f"DELETE EDGE: {fr} to {type} {type}", style='green')

    def deleted_node_obj(self, node: Node):
        console.print(f"DELETED NODE OBJ: {node}", style='green')

    def deleted_edge_obj(self, edge: Edge):
        console.print(f"DELETED EDGE OBJ: {edge}", style='green')

    def connect(self):
        signals.connect2(g, signals.UPDATE_NODE_ATTR, self.update_node_att)
        signals.connect2(g, signals.UPDATE_NODE, self.update_node)
        signals.connect2(g, signals.DELETE_NODE, self.delete_node)
        signals.connect2(g, signals.UPDATE_EDGE, self.update_edge)
        signals.connect2(g, signals.UPDATE_EDGE_ATTR, self.update_edge_att)
        signals.connect2(g, signals.DELETE_EDGE, self.delete_edge)
        signals.connect2(g, signals.DELETE_NODE_OBJ, self.deleted_node_obj)
        signals.connect2(g, signals.DELETE_EDGE_OBJ, self.deleted_edge_obj)

app = QtWidgets.QApplication(sys.argv)


# =============== DSR SLOTS  ================
# =============================================
def update_node_att(id: int, attribute_names: [str]):
    console.print(f"UPDATE NODE ATT: {id} {attribute_names}", style='green')

def update_node(id: int, type: str):
    console.print(f"UPDATE NODE: {id} {type}", style='green')

def delete_node(id: int):
    console.print(f"DELETE NODE:: {id} ", style='green')

def update_edge(fr: int, to: int, type: str):
    console.print(f"UPDATE EDGE: {fr} to {type}", type, style='green')

def update_edge_att(fr: int, to: int, type: str, attribute_names: [str]):
    console.print(f"UPDATE EDGE ATT: {fr} to {type} {attribute_names}", style='green')

def delete_edge(fr: int, to: int, type: str):
    console.print(f"DELETE EDGE: {fr} to {type} {type}", style='green')

def deleted_node_obj(node: Node):
    console.print(f"DELETED NODE OBJ: {node}", style='green')

def deleted_edge_obj(edge: Edge):
    console.print(f"DELETED EDGE OBJ: {edge}", style='green')


agent = SpecificWorker({})

agent.connect()


signals.connect2(g, signals.UPDATE_NODE_ATTR, update_node_att)
signals.connect2(g, signals.UPDATE_NODE, update_node)
signals.connect2(g, signals.DELETE_NODE, delete_node)
signals.connect2(g, signals.UPDATE_EDGE, update_edge)
signals.connect2(g, signals.UPDATE_EDGE_ATTR, update_edge_att)
signals.connect2(g, signals.DELETE_EDGE, delete_edge)
signals.connect2(g, signals.DELETE_NODE_OBJ, deleted_node_obj)
signals.connect2(g, signals.DELETE_EDGE_OBJ, deleted_edge_obj)



console.print("DELETE EDGE", style="green")

result = g.delete_edge(1, 2, "RT")
console.print(result)

console.print("DELETE NODE", style="green")

result = g.delete_node(2)
console.print(result)