//
// Created by jc on 19/11/22.
//

#pragma once

#include "dsr/core/rtps/dsrparticipant.h"
#include "dsr/core/topics/IDLGraphPubSubTypes.h"
#include "dsr/core/types/crdt_types.h"

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#define TIMEOUT 5000

namespace DSR
{
    class Graph;

    class Transport;

    class BaseManager
    {
    protected:
        std::weak_ptr<Transport> transport;

    public:
        virtual auto set_transport(std::weak_ptr<Transport> ptr) -> void = 0;
        virtual auto stop() -> void = 0;

        //////////////////////////////////////////////////
        ///// topics
        /////////////////////////////////////////////////

        virtual auto start_fullgraph_request(Graph *) -> std::pair<bool, bool> = 0;

        virtual auto start_fullgraph_server(Graph *) -> void = 0;
        
        virtual auto start_pubs(Graph *, bool show) -> void = 0;
        
        virtual auto start_subs(Graph *, bool show) -> void = 0;

        //////////////////////////////////////////////////
        ///// write
        /////////////////////////////////////////////////

        virtual auto write_node(mvreg<DSR::CRDTNode>&& node, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool = 0;
        
        virtual auto write_edge(mvreg<DSR::CRDTEdge>&& edge, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool = 0;
        
        virtual auto write_node_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool = 0;
        
        virtual auto write_edge_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool = 0;
        
        virtual auto write_graph(std::map<uint64_t, DSR::CRDTNode> &&graph, std::vector<uint64_t> deleted_nodes) -> bool = 0;
        
        virtual auto write_request(std::string& agent_name, uint32_t agent_id) -> bool = 0;
    };

    class Transport : std::enable_shared_from_this<Transport>
    {

        Transport(std::unique_ptr<BaseManager> comm_);

    public:
        std::unique_ptr<BaseManager> comm;
        mutable std::mutex participant_set_mutex;
        std::unordered_map<std::string, bool> participant_set;

        static auto create(std::unique_ptr<BaseManager> comm_) -> std::shared_ptr<Transport>;

        auto stop() -> void;

        //////////////////////////////////////////////////
        ///// Topics
        /////////////////////////////////////////////////
        auto start_fullgraph_request(Graph *graph) -> std::pair<bool, bool>;

        auto start_fullgraph_server(Graph *graph) -> void;

        auto start_topics_subcription(Graph *graph, bool show) -> void;

        auto start_topics_publishing(Graph *graph, bool show) -> void;

        //////////////////////////////////////////////////
        ///// Agents info
        /////////////////////////////////////////////////
        auto get_connected_agents() const -> std::vector<std::string>;

        //////////////////////////////////////////////////
        ///// Write
        /////////////////////////////////////////////////
        auto write_node(mvreg<DSR::CRDTNode>&& node, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool;

        auto write_edge(mvreg<DSR::CRDTEdge>&& edge, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool;

        auto write_node_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool;

        auto write_edge_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool;

        auto write_graph(std::map<uint64_t, DSR::CRDTNode> &&graph, std::vector<uint64_t> deleted_nodes) -> bool;

        auto write_request(std::string& agent_name, uint32_t agent_id) -> bool;
    };

}  // namespace DSR
