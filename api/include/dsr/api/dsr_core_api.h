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
#include <any>
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
#include "dsr/core/id_generator.h"


#define TIMEOUT 5000

namespace DSR
{

    class RT_API;

    struct SignalInfo
    {
        uint32_t agent_id; //change agent_id
        //uint64_t send_timestamp; //send timestamp from the middleware
        //uint64_t recv_timestamp; //recv timestamp from the middleware
    };

    inline static constexpr SignalInfo default_signal_info {};

    /////////////////////////////////////////////////////////////////
    /// GRAPH UNSAFE API
    /////////////////////////////////////////////////////////////////
    class Graph : public QObject
    {
        friend RT_API;
        Q_OBJECT

    public:
        Graph(const CortexConfig& cfg);
        ~Graph() override;


        //////////////////////////////////////////////////////
        ///  Graph API
        //////////////////////////////////////////////////////

        // Utils
        size_t size();
        bool empty(const uint64_t &id);
        std::map<uint64_t, Node> get_copy() const;

        //////////////////////////////////////////////////////
        ///  CONVENIENCE METHODS
        //////////////////////////////////////////////////////

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


        void update_maps_node_delete(uint64_t id, const std::optional<CRDTNode>& n);
        void update_maps_node_insert(uint64_t id, const CRDTNode &n);
        void update_maps_edge_delete(uint64_t from, uint64_t to, const std::string &key = "");
        void update_maps_edge_insert(uint64_t from, uint64_t to, const std::string &key);


        //////////////////////////////////////////////////////////////////////////
        // Non-blocking graph operations
        //////////////////////////////////////////////////////////////////////////
        std::optional<CRDTNode> get_(uint64_t id);
        std::optional<CRDTEdge> get_edge_(uint64_t from, uint64_t to, const std::string &key);
        std::tuple<bool, std::optional<IDL::MvregNode>> insert_node_(CRDTNode &&node);
        std::tuple<bool, std::optional<std::vector<IDL::MvregNodeAttr>>> update_node_(CRDTNode &&node);
        std::tuple<bool, std::vector<std::tuple<uint64_t, uint64_t, std::string>>, std::optional<IDL::MvregNode>, std::vector<IDL::MvregEdge>> delete_node_(uint64_t id);
        std::optional<IDL::MvregEdge> delete_edge_(uint64_t from, uint64_t t, const std::string &key);
        std::tuple<bool, std::optional<IDL::MvregEdge>, std::optional<std::vector<IDL::MvregEdgeAttr>>> insert_or_assign_edge_(CRDTEdge &&attrs, uint64_t from, uint64_t to);

        //////////////////////////////////////////////////////////////////////////
        // Other methods
        //////////////////////////////////////////////////////////////////////////
        std::map<uint64_t , IDL::MvregNode> copy_map();

        //////////////////////////////////////////////////////////////////////////
        // CRDT join operations
        ///////////////////////////////////////////////////////////////////////////
        void join_delta_node(IDL::MvregNode &&mvreg);
        void join_delta_edge(IDL::MvregEdge &&mvreg);
        std::optional<std::string> join_delta_node_attr(IDL::MvregNodeAttr &&mvreg);
        std::optional<std::string> join_delta_edge_attr(IDL::MvregEdgeAttr &&mvreg);
        void join_full_graph(IDL::OrMap &&full_graph);

        bool process_delta_edge(uint64_t from, uint64_t to, const std::string& type, mvreg<CRDTEdge> && delta);
        void process_delta_node_attr(uint64_t id, const std::string& att_name, mvreg<CRDTAttribute> && attr);
        void process_delta_edge_attr(uint64_t from, uint64_t to, const std::string& type, const std::string& att_name, mvreg<CRDTAttribute> && attr);



    private:

        Graph(const Graph& G); //Private constructor for DSRCopy

       
        //////////////////////////////////////////////////////////////////////////
        // Other
        //////////////////////////////////////////////////////////////////////////
        
        CortexConfig config;
        id_generator generator;
        

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

        std::unordered_map<std::string, uint64_t> name_map;     // mapping between name and id of nodes.
        std::unordered_map<uint64_t, std::string> id_map;       // mapping between id and name of nodes.
        std::unordered_map<std::pair<uint64_t, uint64_t>, std::unordered_set<std::string>, hash_tuple> edges;      // collection with all graph edges. ((from, to), key)
        std::unordered_map<uint64_t , std::unordered_set<std::pair<uint64_t, std::string>,hash_tuple>> to_edges;      // collection with all graph edges. (to, (from, key))
        std::unordered_map<std::string, std::unordered_set<std::pair<uint64_t, uint64_t>, hash_tuple>> edgeType;  // collection with all edge types.
        std::unordered_map<std::string, std::unordered_set<uint64_t>> nodeType;  // collection with all node types.


        //////////////////////////////////////////////////////////////////////////
        // Temporaty deltas (needed for async message arrival)
        //////////////////////////////////////////////////////////////////////////

        std::unordered_multimap<uint64_t, std::tuple<std::string, mvreg<DSR::CRDTAttribute>, uint64_t> > unprocessed_delta_node_att;
        std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<DSR::CRDTEdge>, uint64_t>> unprocessed_delta_edge_from;
        std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<DSR::CRDTEdge>, uint64_t>> unprocessed_delta_edge_to;
        std::unordered_multimap<std::tuple<uint64_t, uint64_t, std::string>, std::tuple<std::string, mvreg<DSR::CRDTAttribute>, uint64_t>, hash_tuple> unprocessed_delta_edge_att;

    signals:
        void update_node_signal(uint64_t, const std::string &type, DSR::SignalInfo info = default_signal_info);
        void update_node_attr_signal(uint64_t id ,const std::vector<std::string>& att_names, DSR::SignalInfo info = default_signal_info);

        void update_edge_signal(uint64_t from, uint64_t to, const std::string &type, DSR::SignalInfo info = default_signal_info);
        void update_edge_attr_signal(uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, DSR::SignalInfo info = default_signal_info);

        void del_edge_signal(uint64_t from, uint64_t to, const std::string &edge_tag, DSR::SignalInfo info = default_signal_info);
        void del_node_signal(uint64_t id, DSR::SignalInfo info = default_signal_info) ;

    };
} // namespace CRDT

Q_DECLARE_METATYPE(DSR::SignalInfo)
