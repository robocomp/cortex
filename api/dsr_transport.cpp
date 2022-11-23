//
// Created by jc on 19/09/22.
//

#include <dsr/api/dsr_transport.h>
#include <functional>
#include <future>

using namespace std::literals;

namespace DSR {


    Transport::Transport(BaseManager *comm_) 
        : comm(std::unique_ptr<BaseManager>(comm_))
    {};

    auto Transport::create(BaseManager *comm_) -> std::shared_ptr<Transport>
    {
        return std::make_shared<Transport>(comm_);
    }


    auto Transport::stop() -> void
    {
        comm->stop();
    }


    //////////////////////////////////////////////////
    ///// Topics
    /////////////////////////////////////////////////
    auto Transport::start_fullgraph_request(Graph* graph) -> std::pair<bool, bool> 
    { 
        return comm->start_fullgraph_request(graph); 
    }
    
    auto Transport::start_fullgraph_server(Graph* graph) -> void 
    {  
        comm->start_fullgraph_server(graph); 
    }

    auto Transport::start_topics_subcription(Graph* graph, bool show) -> void 
    { 
        comm->start_pubs(graph, show);
    }

    auto Transport::start_topics_publishing(Graph* graph, bool show) -> void 
    {
        comm->start_subs(graph, show);
    }


    //////////////////////////////////////////////////
    ///// Agents info
    /////////////////////////////////////////////////
    auto Transport::get_connected_agents() const -> std::vector<std::string>
    {
        std::unique_lock<std::mutex> lck(participant_set_mutex);
        std::vector<std::string> ret_vec;
        ret_vec.reserve(participant_set.size());
        for (auto &[k, _]: participant_set)
        {
            ret_vec.emplace_back(k);
        }
        return ret_vec;
    }


    //////////////////////////////////////////////////
    ///// write
    /////////////////////////////////////////////////

    auto Transport::write_node(IDL::MvregNode *node) -> bool
    {
        return comm->write_node(node);
    }

    auto Transport::write_edge(IDL::MvregEdge *edge) -> bool 
    {
        return comm->write_edge(edge);
    }

    auto Transport::write_node_attributes(std::vector<IDL::MvregNodeAttr> *attributes) -> bool
    {
        return comm->write_node_attributes(attributes);
    }
    
    auto Transport::write_edge_attributes(std::vector<IDL::MvregEdgeAttr> *attributes) -> bool
    {
        return comm->write_edge_attributes(attributes);
    }
    
    auto Transport::write_graph(IDL::OrMap *map) -> bool
    {
        return comm->write_graph(map);
    }
    
    auto Transport::write_request(IDL::GraphRequest* request) -> bool
    {
        return comm->write_request(request);
    }
        
}