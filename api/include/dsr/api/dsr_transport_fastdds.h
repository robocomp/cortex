//
// Created by jc on 19/11/22.
//


#pragma once 

#include "threadpool/threadpool.h"
#include <dsr/api/dsr_transport.h>


namespace DSR
{

    class FastDDSTransport : public BaseManager 
    {

    public:

        using DDSReader = eprosima::fastdds::dds::DataReader;
        using FnDDSReader =  std::function<void(DDSReader* reader,  Graph *graph)>;
        using DDSParticipantInfo = eprosima::fastrtps::rtps::ParticipantDiscoveryInfo;
        using FnParticipantChange = std::function<void(Graph *graph_, DDSParticipantInfo&&)>;

        FastDDSTransport();


        auto set_transport(std::weak_ptr<Transport> ptr) -> void override;
        auto stop() -> void override;

        auto start_fullgraph_request(Graph* graph) -> std::pair<bool, bool> override;
        auto start_fullgraph_server(Graph* graph) -> void                   override;
        auto start_subs(Graph* graph, bool show) -> void                  override;
        auto start_pubs(Graph* graph, bool show) -> void                  override;


        auto start_node_subscription(Graph* graph, bool show) -> void;
        auto start_edge_subscription(Graph* graph, bool show) -> void;
        auto start_node_attrs_subscription(Graph* graph, bool show) -> void;
        auto start_edge_attrs_subscription(Graph* graph, bool show) -> void;


        auto write_node(mvreg<DSR::CRDTNode>&& node, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool override;
        auto write_edge(mvreg<DSR::CRDTEdge>&& edge, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool override;
        auto write_node_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool override;
        auto write_edge_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool override;
        auto write_graph(std::map<uint64_t, DSR::CRDTNode> &&graph, std::vector<uint64_t> deleted_nodes) -> bool override;
        auto write_request(std::string& agent_name, uint32_t agent_id) -> bool override;

    private:

        //Custom function for each DDS topic
        class NewMessageFn 
        {
            Graph *graph;
            FnDDSReader f;
        public:
            NewMessageFn() = delete;
            NewMessageFn(Graph *graph_, FnDDSReader f_)
                    : graph(graph_), f(std::move(f_)) {}


            void operator()(DDSReader* reader) const { f(reader, graph); };
        };

        class ParticipantChangeFn 
        {
            Graph *graph;
            FnParticipantChange f;
        public:
            
            ParticipantChangeFn() = delete;
            ParticipantChangeFn(Graph *graph_, FnParticipantChange f_)
                    : graph(graph_), f(std::move(f_)) {}


            void operator()(DDSParticipantInfo&& info) const
            {
                f(graph, std::forward<DDSParticipantInfo&&>(info));
            };
        };

        struct DDSPubSub
        {
            DSRPublisher dsr_pub;
            DSRSubscriber dsr_sub;
        };

        DSRParticipant participant;
        DDSPubSub node;
        DDSPubSub node_attribute;
        DDSPubSub edge;
        DDSPubSub edge_attribute;
        DDSPubSub graph_server;
        DDSPubSub graph_request;
        std::mutex mtx_entity_creation;
        ThreadPool tp, tp_delta_attr;
    };

}