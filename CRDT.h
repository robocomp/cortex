//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_NODES
#define CRDT_NODES

#include <iostream>
#include "DSRGraph.h"
#include <map>
namespace CRDT {
    using N = RoboCompDSR::Node;

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

            void addNode(int id, N &node) {
                nodes[id].add(node, id);
    //            emit addNodeSIGNAL(id, content.type);
            };

            void replaceNode(int id, const N &node) {
                nodes.erase(id);
                nodes[id].add(node, id);
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

            int id() { return nodes.getId(); };

            RoboCompDSR::MapAworSet map() {
                RoboCompDSR::MapAworSet m;
                for (auto & kv : nodes.getMap()) { // Map of Aworset to ICE
                    RoboCompDSR::AworSet aworSet;
                    aworSet.id = kv.second.getId();
                    for (auto & kv : kv.second.dots().ds)
                        aworSet.dk.ds[RoboCompDSR::PairInt{kv.first.first, kv.first.second}] = kv.second;
                    for (auto & kv : kv.second.dots().cbase.getCcDc().first)
                        aworSet.dk.cbase.cc[kv.first] = kv.second;
                    for (auto & kv : kv.second.dots().cbase.getCcDc().second)
                        aworSet.dk.cbase.dc.insert(RoboCompDSR::PairInt{kv.first, kv.second});
                    m[kv.first] = aworSet;
                }
                return m;
            };

            RoboCompDSR::DotContext context() { // Context to ICE
                RoboCompDSR::DotContext om_dotcontext;
                for (auto & kv : nodes.context().getCcDc().first)
                    om_dotcontext.cc[kv.first] = kv.second;
                for (auto & kv : nodes.context().getCcDc().second)
                    om_dotcontext.dc.insert(RoboCompDSR::PairInt{kv.first, kv.second});
                return om_dotcontext;
            }

        private:
            Nodes nodes;
        };
}

#endif //DSR_GRAPH_DSRGRAPHW_H
