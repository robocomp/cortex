//
// Created by jc on 19/11/22.
//


#pragma once 

#include "dsr/core/topics/IDLGraph.h"
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

        FastDDSTransport(std::weak_ptr<Transport> _transport);

        auto start_fullgraph_request(Graph* graph) -> std::pair<bool, bool> override;
        auto start_fullgraph_server(Graph* graph) -> void                   override;
        auto start_topics(Graph* graph, bool show) -> void                  override;

        auto start_node_subscription(Graph* graph, bool show) -> void;
        auto start_edge_subscription(Graph* graph, bool show) -> void;
        auto start_node_attrs_subscription(Graph* graph, bool show) -> void;
        auto start_edge_attrs_subscription(Graph* graph, bool show) -> void;


        auto write_node(IDL::MvregNode *node) -> bool override;
        auto write_edge(IDL::MvregEdge *edge) -> bool override;
        auto write_node_attributes(std::vector<IDL::MvregNodeAttr> *attributes) -> bool override;
        auto write_edge_attributes(std::vector<IDL::MvregEdgeAttr> *attributes) -> bool override;
        auto write_graph(IDL::OrMap *map) -> bool override;
        auto write_request(IDL::GraphRequest* request) -> bool override;

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

        DSRParticipant dsrparticipant;
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