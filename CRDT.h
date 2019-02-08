//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_GRAPH
#define CRDT_GRAPH

#include <iostream>
#include <map>
#include <chrono>
#include <thread>
#include "graph.h"
#include "libs/DSRGraph.h"
#include "libs/delta-crdts.cc"
#include <DataStorm/DataStorm.h>

#define TIMEOUT 5

namespace CRDT {

    using N = RoboCompDSR::Node;
    using Nodes = ormap<int, aworset<N, int>, int>;

    /////////////////////////////////////////////////////////////////
    /// CRDT API
    /////////////////////////////////////////////////////////////////

    class CRDTGraph {
        public:
            CRDTGraph(int root, std::string name, std::shared_ptr<DSR::Graph> graph_);
            ~CRDTGraph();

            void insert_or_assign(int id, const std::string &type_);
            void insert_or_assign(int id, const N &node);
            void add_node_attribs(int id, const RoboCompDSR::Attribs &att);
            void add_edge_attribs(int from, int to, const RoboCompDSR::Attribs &att);
            void replace_node(int id, const N &node);
            void add_edge(int from, int to, const std::string &label_);
            void join_full_graph(RoboCompDSR::OrMap full_graph);
            void join_delta_node(RoboCompDSR::AworSet aworSet);

            N get(int id);

            // Tools
            void print();

            void start_subscription_thread();
            void start_fullgraph_server_thread();
            void start_fullgraph_request_thread();


        private:
            Nodes nodes; // Main data
            int graph_root;
            bool work;

            std::thread read_thread, request_thread, server_thread; // Threads

            DataStorm::Node node; // Main datastorm node

            std::string agent_name, filter;

            std::shared_ptr<DataStorm::SingleKeyWriter<std::string, RoboCompDSR::AworSet >> writer;
            std::shared_ptr<DataStorm::Topic<std::string, RoboCompDSR::AworSet >> topic;

            void subscription_thread();
            void fullgraph_server_thread();
            void fullgraph_request_thread();

            RoboCompDSR::AworSet translateAwCRDTtoICE(int id, aworset<N, int> &data);
            aworset<N, int> translateAwICEtoCRDT(int id, RoboCompDSR::AworSet &data);
            void createIceGraphFromDSRGraph(std::shared_ptr<DSR::Graph> graph);

            void clear();
            int id();
            RoboCompDSR::MapAworSet map();
            RoboCompDSR::DotContext context();

    };
}

#endif
