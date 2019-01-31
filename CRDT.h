//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_NODES
#define CRDT_NODES

#include <iostream>
#include "DSRGraph.h"
#include <map>
#include <typeinfo>

namespace CRDT {
    using N = RoboCompDSR::Node;

    class CRDTNodes {
        public:

            using Nodes = ormap<int, aworset<N, int>, int>; // Implement root as node zero. (0)

            CRDTNodes(int root) { nodes = Nodes(root);}

            ////////////////////////////////////////////////////////////////////////////////////////////
            //									CRDT API
            ////////////////////////////////////////////////////////////////////////////////////////////
            RoboCompDSR::AworSet addNode(int id, const std::string &type_) {
                N new_node;
                new_node.type = type_;
                new_node.id = id;
                new_node.attrs.insert(std::make_pair("name", std::string("unknown")));
                auto delta = nodes[id].add(new_node);

                //            emit addNodeSIGNAL(id, type_);
                return translateAwCRDTtoICE(delta);
            };

            RoboCompDSR::AworSet addNode(int id, N &node) {
                cout << __PRETTY_FUNCTION__ <<" "<<node<<endl;
                auto delta = nodes[id].add(node, id);

                //            emit addNodeSIGNAL(id, content.type);
                cout << __FUNCTION__ << " Delta: "<<delta<< " \nNodes: " << nodes << endl;
                return translateAwCRDTtoICE(delta);
            };

            void joinDeltaGraph() {
                //TODO
            }

            void joinDeltaNode(RoboCompDSR::AworSet aworSet) {
                aworset<N, int> aw = translateAwICEtoCRDT(aworSet);
                nodes[aw.getId()].join(aw);
                cout << __FUNCTION__ << " " << aw << "\n" << nodes << endl;
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
                for (auto &kv : nodes.getMap())  // Map of Aworset to ICE
                    m[kv.first] = translateAwCRDTtoICE(kv.second);
                return m;
            };

            RoboCompDSR::DotContext context() { // Context to ICE
                RoboCompDSR::DotContext om_dotcontext;
                for (auto &kv_cc : nodes.context().getCcDc().first)
                    om_dotcontext.cc[kv_cc.first] = kv_cc.second;
                for (auto &kv_dc : nodes.context().getCcDc().second)
                    om_dotcontext.dc.push_back(RoboCompDSR::PairInt{kv_dc.first, kv_dc.second});
                return om_dotcontext;
            }

        private:
            Nodes nodes;

            RoboCompDSR::AworSet translateAwCRDTtoICE(aworset<N, int> &data) {
                cout << __FUNCTION__ << " Me llega: " << data << endl;
                RoboCompDSR::AworSet delta_crdt;
                delta_crdt.id = data.getId();
                for (auto &kv_dots : data.dots().ds)
                    delta_crdt.dk.ds[RoboCompDSR::PairInt{kv_dots.first.first, kv_dots.first.second}] = kv_dots.second;
                for (auto &kv_cc : data.context().getCcDc().first)
                    delta_crdt.dk.cbase.cc[kv_cc.first] = kv_cc.second;
                for (auto &kv_dc : data.context().getCcDc().second)
                    delta_crdt.dk.cbase.dc.push_back(RoboCompDSR::PairInt{kv_dc.first, kv_dc.second});
                cout << __FUNCTION__ << " Devuelvo: " << delta_crdt << endl;
                return delta_crdt;
            }

            aworset<N, int> translateAwICEtoCRDT(RoboCompDSR::AworSet &data){
                cout << __FUNCTION__ << " Me llega: " << data << endl;
                dotcontext<int> dotcontext_aux;
                auto m = static_cast<std::map<int,int>>(data.dk.cbase.cc);
                set<pair<int,int>> s;
                for (auto &v : data.dk.cbase.dc)
                    s.insert(std::make_pair(v.first,v.second));
                dotcontext_aux.setContext(m, s);
                cout << "Contexto generado: " << dotcontext_aux << endl;
//                aworset<N, int> aw = aworset<N, int> (data.id, dotcontext_aux);
                aworset<N, int> aw = aworset<N, int>(data.id);
                aw.setContext(dotcontext_aux);
                aw.add(data.dk.ds.at((*(data.dk.ds.rbegin())).first));
//                cout << "AW lleno: "<< aw << endl;

                cout << __FUNCTION__ << " Devuelvo: " << aw << endl;
                return aw;
            }


    };
}

#endif //DSR_GRAPH_DSRGRAPHW_H
