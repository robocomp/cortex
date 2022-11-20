//
// Created by jc on 19/11/22.
//

#pragma once


#include <memory>
#include <optional>
#include <unordered_map>
#include <mutex>

#include "dsr/core/rtps/dsrparticipant.h"
#include "dsr/core/topics/IDLGraph.h"
#include "dsr/core/types/crdt_types.h"

#define TIMEOUT 5000

namespace DSR
{
    class Graph;
        /*
        auto get_agent_id() -> uint64_t;

        auto copy_map() -> std::map<uint64_t , IDL::MvregNode>;

        auto join_delta_node(IDL::MvregNode &&mvreg) -> void;
        auto join_delta_edge(IDL::MvregEdge &&mvreg) -> void;
        auto join_delta_node_attr(IDL::MvregNodeAttr &&mvreg) -> std::optional<std::string>;
        auto join_delta_edge_attr(IDL::MvregEdgeAttr &&mvreg) -> std::optional<std::string>;
        auto join_full_graph(IDL::OrMap &&full_graph) -> void;*/

    class Transport;

    class BaseManager
    {
        protected:

        std::weak_ptr<Transport> transport;
        
        public:

        //////////////////////////////////////////////////
        ///// topics
        /////////////////////////////////////////////////

        virtual auto start_fullgraph_request(Graph*) -> std::pair<bool, bool> = 0;
        virtual auto start_fullgraph_server(Graph*) -> void  = 0;
        virtual auto start_topics(Graph*, bool show) -> void = 0;


        //////////////////////////////////////////////////
        ///// write
        /////////////////////////////////////////////////

        virtual auto write_node(IDL::MvregNode *node) -> bool = 0;
        virtual auto write_edge(IDL::MvregEdge *edge) -> bool = 0;
        virtual auto write_node_attributes(std::vector<IDL::MvregNodeAttr> *attributes) -> bool = 0;
        virtual auto write_edge_attributes(std::vector<IDL::MvregEdgeAttr> *attributes) -> bool = 0;
        virtual auto write_graph(IDL::OrMap *map) -> bool = 0;
        virtual auto write_request(IDL::GraphRequest* request) -> bool = 0;
        

    };


    class Transport : std::enable_shared_from_this<Transport> 
    {

        Transport(BaseManager *comm_);

    public:
        std::unique_ptr<BaseManager> comm;
        mutable std::mutex participant_set_mutex;
        std::unordered_map<std::string, bool> participant_set;
 
        auto create(BaseManager *comm_) -> std::shared_ptr<Transport>;
        
        //////////////////////////////////////////////////
        ///// Topics
        /////////////////////////////////////////////////
        auto start_fullgraph_request(Graph* graph) -> std::pair<bool, bool> ;
        
        auto start_fullgraph_server(Graph* graph) -> void ;

        auto start_topics(Graph* graph, bool show) -> void ;

        //////////////////////////////////////////////////
        ///// Agents info
        /////////////////////////////////////////////////
        auto get_connected_agents() const -> std::vector<std::string>; 


        //////////////////////////////////////////////////
        ///// Write
        /////////////////////////////////////////////////
        auto write_node(IDL::MvregNode *node) -> bool;

        auto write_edge(IDL::MvregEdge *edge) -> bool;

        auto write_node_attributes(std::vector<IDL::MvregNodeAttr> *attributes) -> bool;
        
        auto write_edge_attributes(std::vector<IDL::MvregEdgeAttr> *attributes) -> bool;
        
        auto write_graph(IDL::OrMap *map) -> bool;
        
        auto write_request(IDL::GraphRequest* request) -> bool;
        
    };

}

