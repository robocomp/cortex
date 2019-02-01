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

        CRDTNodes(int root) { nodes = Nodes(root); }

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
            return translateAwCRDTtoICE(id, delta);
        };

        RoboCompDSR::AworSet addNode(int id, N &node) {
            auto delta = nodes[id].add(node, id);
            //            emit addNodeSIGNAL(id, content.type);
            return translateAwCRDTtoICE(id, delta);
        };

        void joinFullGraph(RoboCompDSR::OrMap full_graph) {
            // Context
            dotcontext<int> dotcontext_aux;
            auto m = static_cast<std::map<int, int>>(full_graph.cbase.cc);
            std::set <pair<int, int>> s;
            for (auto &v : full_graph.cbase.dc)
                s.insert(std::make_pair(v.first, v.second));
            dotcontext_aux.setContext(m, s);

            //Map
            //TODO: Improve. It is not the most efficient.
            for (auto &v : full_graph.m)
                for (auto &awv : translateAwICEtoCRDT(v.first, v.second).readAsList())
                    nodes[v.first].add(awv.second, v.first);

        }

        void joinDeltaNode(RoboCompDSR::AworSet aworSet) {
            nodes[aworSet.id].join(translateAwICEtoCRDT(aworSet.id, aworSet));
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
            std::cout << "----------------------------------------\n" << nodes
                      << "----------------------------------------" << std::endl;
        };

        int id() { return nodes.getId(); };

        RoboCompDSR::MapAworSet map() {
            RoboCompDSR::MapAworSet m;
            for (auto &kv : nodes.getMap())  // Map of Aworset to ICE
                m[kv.first] = translateAwCRDTtoICE(kv.first, kv.second);
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

        RoboCompDSR::AworSet translateAwCRDTtoICE(int id, aworset<N, int> &data) {
            RoboCompDSR::AworSet delta_crdt;
            for (auto &kv_dots : data.dots().ds)
                delta_crdt.dk.ds[RoboCompDSR::PairInt{kv_dots.first.first, kv_dots.first.second}] = kv_dots.second;
            for (auto &kv_cc : data.context().getCcDc().first)
                delta_crdt.dk.cbase.cc[kv_cc.first] = kv_cc.second;
            for (auto &kv_dc : data.context().getCcDc().second)
                delta_crdt.dk.cbase.dc.push_back(RoboCompDSR::PairInt{kv_dc.first, kv_dc.second});
            delta_crdt.id = id; //TODO: Check K value of aworset (ID=0)
            return delta_crdt;
        }

        aworset<N, int> translateAwICEtoCRDT(int id, RoboCompDSR::AworSet &data) {
            // Context
            dotcontext<int> dotcontext_aux;
            auto m = static_cast<std::map<int, int>>(data.dk.cbase.cc);
            std::set <pair<int, int>> s;
            for (auto &v : data.dk.cbase.dc)
                s.insert(std::make_pair(v.first, v.second));
            dotcontext_aux.setContext(m, s);

            // Dots
            std::map <pair<int, int>, N> ds_aux;
            for (auto &v : data.dk.ds)
                ds_aux[pair<int, int>(v.first.first, v.first.second)] = v.second;

            // Join
            aworset<N, int> aw = aworset<N, int>(id);
            aw.setContext(dotcontext_aux);
            aw.dots().set(ds_aux);
            return aw;
        }


    };
}

#endif //DSR_GRAPH_DSRGRAPHW_H
