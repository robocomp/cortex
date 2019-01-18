//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_NODES
#define CRDT_NODES

#include <iostream>
#include "DSRGraph.h"

namespace CRDT {
    using N = RoboCompDSR::Content;

    struct Node {
        N node;

        friend std::ostream &operator<<(std::ostream &output, const Node &n_) {
            output << n_;
            return output;
        };

    };

    class CRDTNodes {
        public:

            using Nodes = ormap<int, aworset<N, int>, int>; // Implement root as node zero. (0)

            ////////////////////////////////////////////////////////////////////////////////////////////
            //									CRDT API
            ////////////////////////////////////////////////////////////////////////////////////////////
            void addNode(int id, const std::string &type_) {
                N new_node;
                new_node.type = type_;
                new_node.id = id;
                new_node.attrs.insert(std::make_pair("name", std::string("unknown")));
                nodes[id].add(new_node, id);
    //            emit addNodeSIGNAL(id, type_);
            };

            void addNode(int id, N &content) {
                nodes[id].add(content, id);
    //            emit addNodeSIGNAL(id, content.type);
            };

            void replaceNode(int id, const N &content) {
                nodes.erase(id);
                nodes[id].add(content, id);
            };

            void addEdge(int from, int to, const std::string &label_) {
                auto n = *(nodes[from].read().rbegin());
                n.fano.insert(std::pair(to, RoboCompDSR::EdgeAttribs{label_, from, to, RoboCompDSR::Attribs()}));
                nodes[from].add(n);
    //            emit addEdgeSIGNAL(from, to, label_);
            };

            void addNodeAttribs(int id, const RoboCompDSR::Attribs &att) {
                auto n = *(nodes[id].read().rbegin());
                for (auto &[k, v] : att)
                    n.attrs.insert_or_assign(k, v);
                nodes[id].add(n);
    //            emit NodeAttrsChangedSIGNAL(id, att);
            };

            void addEdgeAttribs(int from, int to, const RoboCompDSR::Attribs &att)  //HAY QUE METER EL TAG para desambiguar
            {
                auto n = *(nodes[from].read().rbegin());
                auto &edgeAtts = n.fano.at(to);
                for (auto &[k, v] : att)
                    edgeAtts.attrs.insert_or_assign(k, v);
                nodes[from].add(n);
                //std::cout << "Emit EdgeAttrsChangedSignal " << from << " to " << to << std::endl;
    //            emit EdgeAttrsChangedSIGNAL(from, to);
            };

            void clear() {
                nodes.reset();
            };

            void print() {
                std::cout << "------------------------- \nNodes:" << std::endl;
                std::cout << nodes << endl;
            };

        private:
            Nodes nodes;
        };
}

#endif //DSR_GRAPH_DSRGRAPHW_H
