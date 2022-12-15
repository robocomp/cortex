//
// Created juancarlos 20/11/22.
//

#pragma once

#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <vector>
#include <variant>
#include <typeinfo>
#include <optional>
#include <type_traits>


#include <QtCore/QtCore>

#include "dsr/api/dsr_transport.h"
#include "dsr/core/types/type_checking/dsr_attr_name.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/utils.h"
#include "dsr_transport.h"


#define TIMEOUT 5000

namespace DSR
{

    class DSRGraph;
    class RT_API;
    class FastDDSTransport;

    struct CortexConfig {
        uint32_t agent_id;
        bool localhost;
        bool copy;
        bool init_empty;
        std::string agent_name;
        std::unique_ptr<BaseManager> comm; //this is moved to transport later.
        std::optional<std::string> load_file;
        std::optional<uint64_t> node_root;
        DSRGraph *dsr; //this is a not owned pointer used only to allow sending signals in the transport. 
    };

    inline constexpr std::string_view default_empty_stringv = "";
    inline const std::string default_empty_string = "";

    /////////////////////////////////////////////////////////////////
    /// GRAPH UNSAFE API
    /////////////////////////////////////////////////////////////////
    class Graph
    {
        friend DSRGraph;
        friend RT_API;
        friend FastDDSTransport; //TODO remove this;

    public:
        Graph(CortexConfig& cfg);
        ~Graph();

        auto init(std::function<void(std::string)> read_from_json_file) -> void;

        //TODO: Document functions locking.

        //////////////////////////////////////////////////////
        ///  CONVENIENCE METHODS
        //////////////////////////////////////////////////////
        
        auto size() const -> size_t;
        auto empty(uint64_t id) const -> bool;
        auto get_copy() const -> std::map<uint64_t, Node>;
        auto copy_map() const -> std::map<uint64_t , NodeInfoTuple>;
        auto get_agent_id() const -> uint64_t;
        auto get_config() -> CortexConfig&;
        void reset();

        //////////////////////////////////////////////////////
        ///  Attribute filters
        //////////////////////////////////////////////////////
        template<typename ... Att>
        constexpr void set_ignored_attributes()
        {
            static_assert((is_attr_name<Att> && ...));
            (ignored_attributes.insert(Att::attr_name), ...);
        }

        auto get_ignored_attributes() -> const std::unordered_set<std::string_view>&;

        //////////////////////////////////////////////////////
        //  Attribute maps
        //////////////////////////////////////////////////////
        
        void update_maps_node_delete(uint64_t id, const std::optional<CRDTNode>& n);
        void update_maps_node_insert(uint64_t id, const CRDTNode &n);
        void update_maps_edge_delete(uint64_t from, uint64_t to, const std::string& key = default_empty_string);
        void update_maps_edge_insert(uint64_t from, uint64_t to, const std::string& key);


        //////////////////////////////////////////////////////////////////////////
        // Non-blocking graph operations
        //////////////////////////////////////////////////////////////////////////

        auto get_(uint64_t id) -> std::optional<CRDTNode>;
        auto get_edge_(uint64_t from, uint64_t to, const std::string& key) -> std::optional<CRDTEdge>;
        auto insert_node_(CRDTNode &&node) -> std::tuple<bool, std::optional<NodeInfoTuple>>;
        auto update_node_(CRDTNode &&node) -> std::tuple<bool, std::optional<NodeAttributeVecTuple>>;
        auto delete_node_(uint64_t id) -> std::tuple<bool, std::vector<std::tuple<uint64_t, uint64_t, std::string>>, std::optional<NodeInfoTuple>, std::vector<EdgeInfoTuple>>;
        auto delete_edge_(uint64_t from, uint64_t t, const std::string& key) -> std::optional<EdgeInfoTuple>;
        auto insert_or_assign_edge_(CRDTEdge &&attrs, uint64_t from, uint64_t to) -> std::tuple<bool, std::optional<EdgeInfoTuple>, std::optional<EdgeAttributeVecTuple>>;


        //////////////////////////////////////////////////////////////////////////
        // CRDT join operations
        ///////////////////////////////////////////////////////////////////////////
        void join_delta_node(NodeInfoTuple &&mvreg);
        void join_delta_edge(EdgeInfoTuple &&mvreg);
        auto join_delta_node_attr(NodeInfoTupleAttr &&mvreg) -> std::optional<std::string>;
        auto join_delta_edge_attr(EdgeInfoTupleAttr &&mvreg) -> std::optional<std::string>;
        void join_full_graph(GraphInfoTuple &&full_graph);

        auto process_delta_edge(uint64_t from, uint64_t to, const std::string& type, mvreg<CRDTEdge> && delta) -> bool;
        void process_delta_node_attr(uint64_t id, const std::string& att_name, mvreg<CRDTAttribute> && attr);
        void process_delta_edge_attr(uint64_t from, uint64_t to, const std::string& type, const std::string& att_name, mvreg<CRDTAttribute> && attr);



    private:

        Graph(const Graph& G); //Private constructor for DSRCopy

        //////////////////////////////////////////////////////////////////////////
        // Other
        //////////////////////////////////////////////////////////////////////////
        CortexConfig& config;        

        //////////////////////////////////////////////////////////////////////////
        // Communication
        //////////////////////////////////////////////////////////////////////////

        std::unordered_set<std::string_view> ignored_attributes;
        std::shared_ptr<Transport> transport;

        //////////////////////////////////////////////////////////////////////////
        // Data storage
        //////////////////////////////////////////////////////////////////////////

        mutable std::shared_mutex _mutex_data;
        std::unordered_map<uint64_t, mvreg<CRDTNode>> nodes; //graph representation in a map
        std::unordered_set<uint64_t> deleted;     // deleted nodes, used to avoid insertion after remove.

        //////////////////////////////////////////////////////////////////////////
        // Cache maps
        //////////////////////////////////////////////////////////////////////////

        mutable std::shared_mutex _mutex_cache_maps;

        std::unordered_map<std::string, uint64_t, string_hash, string_equal> name_map;     // mapping between name and id of nodes.
        std::unordered_map<uint64_t, std::string> id_map;       // mapping between id and name of nodes.
        std::unordered_map<std::pair<uint64_t, uint64_t>, std::unordered_set<std::string>, hash_tuple> edges;      // collection with all graph edges. ((from, to), key)
        std::unordered_map<uint64_t , std::unordered_set<std::pair<uint64_t, std::string>,hash_tuple>> to_edges;      // collection with all graph edges. (to, (from, key))
        std::unordered_map<std::string, std::unordered_set<std::pair<uint64_t, uint64_t>, hash_tuple>, string_hash, string_equal> edgeType;  // collection with all edge types.
        std::unordered_map<std::string, std::unordered_set<uint64_t>, string_hash, string_equal> nodeType;  // collection with all node types.


        //////////////////////////////////////////////////////////////////////////
        // Temporaty deltas (needed for async message arrival)
        //////////////////////////////////////////////////////////////////////////

        std::unordered_multimap<uint64_t, std::tuple<std::string, mvreg<DSR::CRDTAttribute>, uint64_t> > unprocessed_delta_node_att;
        std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<DSR::CRDTEdge>, uint64_t>> unprocessed_delta_edge_from;
        std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<DSR::CRDTEdge>, uint64_t>> unprocessed_delta_edge_to;
        std::unordered_multimap<std::tuple<uint64_t, uint64_t, std::string>, std::tuple<std::string, mvreg<DSR::CRDTAttribute>, uint64_t>, hash_tuple> unprocessed_delta_edge_att;

    };
} // namespace CRDT


