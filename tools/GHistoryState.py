from typing import List, Union, Tuple, Dict
from pydsr import GHistory, ChangeInfo, Node, Edge, MapStringEdge

def get_file(filename : str) ->  List[Tuple[ChangeInfo, Union[None, Node, Edge, Dict[int, Node]]]]:
    return GHistory.read_file(filename)

class GHistoryState():
    """
    This class uses the contents of a GHistory api record and allows
    the user to get the content of the graph at any point of the execuion.
    """

    def __init__(self, states: List[Tuple[ChangeInfo, Union[None, Node, Edge, Dict[int, Node]]]]):

        self.states = len(states)
        self.node_states = {}
        self.edge_states = {}
        self.state_change = []

        for (idx, (ci, obj)) in enumerate(states):
            self.state_change.append((ci, obj))
            if ci.op == 0:
                for (inode, node) in obj.items():
                    for (key, edge) in node.edges.items():
                        self.edge_states[(node.id, key[0], key[1])] = [(idx, edge, ci.timestamp, ci.agent_id)]
                    #node.edges = MapStringEdge()
                    self.node_states[inode] = [(idx, node, ci.timestamp, ci.agent_id)]
            elif ci.op == 1:
                if not obj:
                    print("[GHistoryState] Reading Empty node. (", ci.node_or_from_id,")", ci.timestamp, ci.agent_id)
                #else:
                #    obj.edges = MapStringEdge()
                if not self.node_states.get(ci.node_or_from_id):
                    self.node_states[ci.node_or_from_id] = [(idx, obj, ci.timestamp, ci.agent_id)]
                else:
                    self.node_states[ci.node_or_from_id].append((idx, obj, ci.timestamp, ci.agent_id))
            elif ci.op == 2:
                if not obj:
                    print("[GHistoryState] Reading Empty edge. (", ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type, ")",ci.timestamp, ci.agent_id)
                if not self.edge_states.get((ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type)):
                    self.edge_states[(ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type)] = [(idx, obj, ci.timestamp, ci.agent_id)]
                else:
                    self.edge_states[(ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type)].append((idx, obj, ci.timestamp, ci.agent_id))
            elif ci.op == 3:
                self.node_states[ci.node_or_from_id].append((idx, None, ci.timestamp, ci.agent_id))
            elif ci.op == 4:
                self.edge_states[(ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type)].append((idx, None, ci.timestamp, ci.agent_id))
              

    def _lower_bound(self, state : int, arr : List[Union[Tuple[int, Node, int, int], Tuple[int, Edge, int, int]]]) -> Union[int, None]:
        end = len(arr) - 1
        init = 0
        if arr[init][0] > state:
            return -1
        if arr[end][0] <= state:
            return end
        while init < end:
            mid = init + (end- init)//2
            if state < arr[mid][0]:
                end = mid
            elif arr[mid][0] == state:
                return mid
            else:
                init = mid + 1
        init -= 1
        return -1 if arr[init][0] > state else init


    def get_num_states(self) -> int:
        """Return the number of states in the file"""
        return self.states

    def get_node_history(self, node_id : int) -> Union[List[Tuple[int, Node, int, int]], None]:
        """
        Return the history of a node. Each entry includes the state where it is created in the file,
        the node without edges, a timestamp of the moment when the signal is executed and the agent_id of the change (
        this may be not accurate if a change has been processed between the signal sending and the execution of the signal.
        ).
        """
        return self.node_states.get(node_id)

    def get_edge_history(self, from_id : int, to_id : int , e_type : str) -> Union[List[Tuple[int, Node, int, int]], None]:
        """
        Return the history of a node. Each entry includes the state where it is created in the file,
        the edge, a timestamp of the moment when the signal is executed and the agent_id of the change (
        this may be not accurate if a change has been processed between the signal sending and the execution of the signal.
        ).
        """
        return self.edge_states.get((from_id, to_id, e_type))

    def get_state(self, state : int):
        """
        Return a tuple of nodes and edges that represents the state of the graph in a given point.
        """

        assert state >= 0, "state must be greater or greater than 0."
        
        nodes = []
        for k in self.node_states:
            idx = self._lower_bound(state, self.node_states[k])
            if idx is not None:
                nodes.append(self.node_states[k][idx])
        
        edges = []
        for k in self.edge_states:
            idx = self._lower_bound(state, self.edge_states[k])
            if idx is not None:
                edges.append(self.edge_states[k][idx])

        return nodes, edges


    def get_change_state(self, state: int) -> Tuple[Union[Node, Edge, None], Union[Node, Edge, None]]:

        ci, obj = self.state_change[state]
        if ci.op == 0:
            return (None, obj)
        elif ci.op == 1:
            idx = self._lower_bound(state-1, self.node_states[ci.node_or_from_id])
            return (self.node_states[ci.node_or_from_id][idx][1] if idx else None, obj if obj else Node(int(ci.node_or_from_id), " ", " "))
        elif ci.op == 2:
            key = (ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type)
            idx = self._lower_bound(state-1, self.edge_states[key])
            return (self.edge_states[key][self._lower_bound(state-1, self.edge_states[key])][1] if idx else None, obj if obj else Edge(ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type, 0)  )
        elif ci.op == 3:
            return (self.node_states[ci.node_or_from_id][self._lower_bound(state-1, self.node_states[ci.node_or_from_id])][1] , None)
        elif ci.op == 4:
            key = (ci.node_or_from_id, ci.maybe_to_id, ci.maybe_edge_type)
            return (self.edge_states[key][self._lower_bound(state-1, self.edge_states[key])][1] , None)

        return (None, None)

"""
val = get_file("salidaqt3d.dsr")
gh = GHistoryState(val)
nodes, edges = gh.get_state(0)
print(nodes)
print(gh.edge_states)
"""




