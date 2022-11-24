//
// Created by crivac on 5/02/19.
//

#include <dsr/api/dsr_api.h>
#include <iostream>
#include <optional>
#include <unistd.h>
#include <algorithm>
#include <utility>
#include <cmath>

#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <fastrtps/Domain.h>


#include <QtCore/qlogging.h>
#include <QtCore/qdebug.h>

using namespace DSR;

using namespace std::literals;

/////////////////////////////////////////////////
///// PUBLIC METHODS
/////////////////////////////////////////////////

DSRGraph::DSRGraph(std::string name, uint32_t id, const std::string &dsr_input_file, bool all_same_host)
    : generator(id)
        
{

    qDebug() << "Agent name: " << QString::fromStdString(name);
    config.agent_name = std::move(name);
    config.agent_id = id;
    config.copy = false;
    config.localhost = all_same_host;
    config.load_file = (! dsr_input_file.empty()) ? std::make_optional(dsr_input_file) : std::nullopt;
    config.init_empty = false;

    utils =  std::make_unique<Utilities>(this);

}

DSRGraph::~DSRGraph()
{
    qDebug() << "Removing DSRGraph";
}

//////////////////////////////////////
/// NODE METHODS
/////////////////////////////////////

std::optional<DSR::Node> DSRGraph::get_node(const std::string &name)
{
    std::shared_lock<std::shared_mutex> lock(graph->_mutex_data);
    if (name.empty()) return {};
    std::optional<uint64_t> id = get_id_from_name(name);
    if (id.has_value())
    {
        std::optional<CRDTNode> n = graph->get_(id.value());
        if (n.has_value()) return Node(std::move(n.value()));
    }
    return {};
}

std::optional<DSR::Node> DSRGraph::get_node(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lock(graph->_mutex_data);
    std::optional<CRDTNode> n = graph->get_(id);
    if (n.has_value()) return Node(std::move(n.value()));
    return {};
}

template<typename No>
std::optional<uint64_t> DSRGraph::insert_node(No &&node)
    requires (std::is_same_v<std::remove_reference_t<No>, DSR::Node>)
{
    std::optional<IDL::MvregNode> delta;
    bool inserted = false;
    {
        std::unique_lock<std::shared_mutex> lock(graph->_mutex_data);
        std::shared_lock<std::shared_mutex> lck_cache(graph->_mutex_cache_maps);
        uint64_t new_node_id = generator.generate();
        node.id(new_node_id);
        if (node.name().empty() or graph->name_map.contains(node.name()))
            node.name(node.type() + "_" + id_generator::hex_string(new_node_id));
        lck_cache.unlock();
        std::tie(inserted, delta) = graph->insert_node_(user_node_to_crdt(std::forward<No>(node)));
    }
    if (inserted)
    {
        if (!config.copy)
        {
            if (delta.has_value())
            {
                graph->transport->write_node(&delta.value());
                emit graph->update_node_signal(node.id(), node.type(), SignalInfo{static_cast<uint32_t>(config.agent_id)});
                for (const auto &[k, v]: node.fano())
                {
                    emit graph->update_edge_signal(node.id(), k.first, k.second,  SignalInfo{static_cast<uint32_t>(config.agent_id)});
                }
            }
        }
        return node.id();
    }
    return {};
}

template std::optional<uint64_t>  DSRGraph::insert_node<DSR::Node &&>(DSR::Node&&);
template std::optional<uint64_t>  DSRGraph::insert_node<DSR::Node&>(DSR::Node&);


template<typename No>
bool DSRGraph::update_node(No &&node)
requires (std::is_same_v<std::remove_cvref_t<No>, DSR::Node>)
{

    bool updated = false;
    std::optional<std::vector<IDL::MvregNodeAttr>> vec_node_attr;

    {
        std::unique_lock<std::shared_mutex> lock(graph->_mutex_data);
        std::shared_lock<std::shared_mutex> lck_cache(graph->_mutex_cache_maps);
        if (graph->deleted.contains(node.id()))
            throw std::runtime_error(
                    (std::string("Cannot update node in G, " + std::to_string(node.id()) + " is deleted") + __FILE__ +
                     " " + __FUNCTION__ + " " + std::to_string(__LINE__)).data());
        else if (( graph->id_map.contains(node.id()) and graph->id_map.at(node.id()) != node.name()) or
                 ( graph->name_map.contains(node.name()) and graph->name_map.at(node.name()) != node.id()))
            throw std::runtime_error(
                    (std::string("Cannot update node in G, id and name must be unique") + __FILE__ + " " +
                     __FUNCTION__ + " " + std::to_string(__LINE__)).data());
        else if (graph->nodes.contains(node.id())) {
            lck_cache.unlock();
            std::tie(updated, vec_node_attr) = update_node_(user_node_to_crdt(std::forward<No>(node)));
        }
    }
    if (updated) {
        if (!config.copy) {
            if (vec_node_attr.has_value()) {
                graph->transport->write_node_attributes(&vec_node_attr.value());
                emit graph->update_node_signal(node.id(), node.type(), SignalInfo{config.agent_id});
                std::vector<std::string> atts_names(vec_node_attr->size());
                std::transform(std::make_move_iterator(vec_node_attr->begin()),
                               std::make_move_iterator(vec_node_attr->end()),
                               atts_names.begin(),
                               [](auto &&x) { return x.attr_name(); });
                emit update_node_attr_signal(node.id(), atts_names, SignalInfo{agent_id});

            }
        }
    }
    return updated;
}

template bool DSRGraph::update_node<DSR::Node &&>(DSR::Node&&);
template bool DSRGraph::update_node<DSR::Node&>(DSR::Node&);
template bool DSRGraph::update_node<const DSR::Node&>(const DSR::Node&);
template bool DSRGraph::update_node<DSR::Node>(DSR::Node&&);



bool DSRGraph::delete_node(const DSR::Node &node)
{
    return delete_node(node.id());
}

bool DSRGraph::delete_node(const std::string &name)
{

    bool result = false;
    std::vector<std::tuple<uint64_t, uint64_t, std::string>> deleted_edges;
    std::optional<IDL::MvregNode> deleted_node;
    std::vector<IDL::MvregEdge> delta_vec;

    std::optional<uint64_t> id = {};
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        id = get_id_from_name(name);
        if (id.has_value()) {
            std::tie(result, deleted_edges, deleted_node, delta_vec) = delete_node_(id.value());
        } else {
            return false;
        }
    }

    if (result) {
        if (!copy) {
            emit del_node_signal(id.value(), SignalInfo{agent_id});
            dsrpub_node.write(&deleted_node.value());

            for (auto &a : delta_vec) {
                dsrpub_edge.write(&a);
            }
            for (auto &[id0, id1, label] : deleted_edges)
                    emit del_edge_signal(id0, id1, label, SignalInfo{ agent_id });
        }
        return true;
    }
    return false;

}

bool DSRGraph::delete_node(uint64_t id)
{

    bool result = false;
    std::vector<std::tuple<uint64_t, uint64_t, std::string>> deleted_edges;
    std::optional<IDL::MvregNode> deleted_node;
    std::vector<IDL::MvregEdge> delta_vec;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::tie(result, deleted_edges, deleted_node, delta_vec) = delete_node_(id);
    }

    if (result) {
        if (!copy) {
            emit del_node_signal(id, SignalInfo{ agent_id });
            dsrpub_node.write(&deleted_node.value());

            for (auto &a  : delta_vec) {
                dsrpub_edge.write(&a);
            }
            for (auto &[id0, id1, label] : deleted_edges) {
                    emit del_edge_signal(id0, id1, label, SignalInfo{ agent_id });
            }
        }
        return true;
    }

    return false;
}


std::vector<DSR::Node> DSRGraph::get_nodes_by_type(const std::string &type)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);

    std::vector<Node> nodes_;
    if (nodeType.contains(type))
    {
        for (auto &id: nodeType.at(type))
        {
            std::optional<CRDTNode> n = graph->get_(id);
            if (n.has_value())
                nodes_.emplace_back(std::move(n.value()));
        }
    }
    return nodes_;
}

std::vector<DSR::Node> DSRGraph::get_nodes_by_types(const std::vector<std::string> &types)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);

    std::vector<Node> nodes_;
    for (auto &type : types)
    {
        if (nodeType.contains(type))
        {
            for (auto &id: nodeType.at(type))
            {
                std::optional<CRDTNode> n = graph->get_(id);
                if (n.has_value())
                    nodes_.emplace_back(std::move(n.value()));
            }
        }
    }
    return nodes_;
}

//////////////////////////////////////////////////////////////////////////////////
// EDGE METHODS
//////////////////////////////////////////////////////////////////////////////////

std::optional<DSR::Edge> DSRGraph::get_edge(const std::string &from, const std::string &to, const std::string &key)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<uint64_t> id_from = get_id_from_name(from);
    std::optional<uint64_t> id_to = get_id_from_name(to);
    if (id_from.has_value() and id_to.has_value())
    {
        auto edge_opt = get_edge_(id_from.value(), id_to.value(), key);
        if (edge_opt.has_value()) return Edge(edge_opt.value());
    }
    return {};
}

std::optional<DSR::Edge> DSRGraph::get_edge(uint64_t from, uint64_t to, const std::string &key)
{
    auto edge_opt = get_edge_(from, to, key);
    if (edge_opt.has_value()) return Edge(std::move(edge_opt.value()));
    return {};
}

std::optional<Edge> DSRGraph::get_edge(const Node &n, const std::string &to, const std::string &key)
{
    std::optional<uint64_t> id_to = get_id_from_name(to);
    if (id_to.has_value())
    {
        return (n.fano().contains({id_to.value(), key})) ?
               std::make_optional(n.fano().find({id_to.value(), key})->second) :
               std::nullopt;
    }
    return {};
}

std::optional<Edge> DSRGraph::get_edge(const Node &n, uint64_t to, const std::string &key)
{
    return (n.fano().contains({to, key})) ?
           std::make_optional(n.fano().find({to, key})->second) :
           std::nullopt;
}

template<typename Ed>
bool DSRGraph::insert_or_assign_edge(Ed &&attrs)
requires (std::is_same_v<std::remove_cvref_t<Ed>, DSR::Edge>)
{
    bool result = false;
    std::optional<IDL::MvregEdge> delta_edge;
    std::optional<std::vector<IDL::MvregEdgeAttr>> delta_attrs;

    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        uint64_t from = attrs.from();
        uint64_t to = attrs.to();
        if (nodes.contains(from) && nodes.contains(to)) {
            std::tie(result, delta_edge, delta_attrs) = insert_or_assign_edge_(user_edge_to_crdt(std::forward<Ed>(attrs)), from, to);
        } else {
            std::cout << __FUNCTION__ << ":" << __LINE__ << " Error. ID:" << from << " or " << to
                      << " not found. Cant update. " << std::endl;
            return false;
        }
    }
    if (result) {
        if (!copy) {
            emit update_edge_signal(attrs.from(), attrs.to(), attrs.type(), SignalInfo{ agent_id });

            if (delta_edge.has_value()) { //Insert
                dsrpub_edge.write(&delta_edge.value());
            }
            if (delta_attrs.has_value()) { //Update
                dsrpub_edge_attrs.write(&delta_attrs.value());
                std::vector<std::string> atts_names(delta_attrs->size());
                std::transform(std::make_move_iterator(delta_attrs->begin()),
                               std::make_move_iterator(delta_attrs->end()),
                               atts_names.begin(),
                               [](auto &&x) { return x.attr_name(); });

                emit update_edge_attr_signal(attrs.from(), attrs.to(), attrs.type(), atts_names, SignalInfo{ agent_id });

            }
        }
    }
    return true;
}



template bool DSRGraph::insert_or_assign_edge<DSR::Edge &&>(DSR::Edge&&);
template bool DSRGraph::insert_or_assign_edge<DSR::Edge&>(DSR::Edge&);
template bool DSRGraph::insert_or_assign_edge<const DSR::Edge&>(const DSR::Edge&);



bool DSRGraph::delete_edge(uint64_t from, uint64_t to, const std::string &key)
{

    std::optional<IDL::MvregEdge> delta;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        delta = delete_edge_(from, to, key);
    }
    if (delta.has_value())
    {
        if (!copy) {
            emit del_edge_signal(from, to, key, SignalInfo{ agent_id });
            dsrpub_edge.write(&delta.value());
        }
        return true;
    }
    return false;
}

bool DSRGraph::delete_edge(const std::string &from, const std::string &to, const std::string &key)
{

    std::optional<uint64_t> id_from = {};
    std::optional<uint64_t> id_to = {};
    std::optional<IDL::MvregEdge> delta;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        id_from = get_id_from_name(from);
        id_to = get_id_from_name(to);
        if (id_from.has_value() && id_to.has_value())
        {
            delta = delete_edge_(id_from.value(), id_to.value(), key);
        }
    }
    if (delta.has_value())
    {
        if (!copy) {
            emit del_edge_signal(id_from.value(), id_to.value(), key, SignalInfo{ agent_id });
            dsrpub_edge.write(&delta.value());
        }
        return true;
    }
    return false;
}


std::vector<DSR::Edge> DSRGraph::get_node_edges_by_type(const Node &node, const std::string &type)
{
    std::vector<Edge> edges_;
    for (auto &[key, edge] : node.fano())
        if (key.second == type)
            edges_.emplace_back(Edge(edge));
    return edges_;
}

std::vector<DSR::Edge> DSRGraph::get_edges_by_type(const std::string &type)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    std::vector<Edge> edges_;
    if (edgeType.contains(type)) {
        for (auto &[from, to] : edgeType.at(type)) {
            auto n = get_edge_(from, to, type);
            if (n.has_value())
                edges_.emplace_back(Edge(std::move(n.value())));
        }
    }
    return edges_;
}

std::vector<DSR::Edge> DSRGraph::get_edges_to_id(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    std::vector<Edge> edges_;
    if (to_edges.contains(id)) {
        for (const auto &[k, v] : to_edges.at(id)) {
            auto n = get_edge_(k, id, v);
            if (n.has_value())
                edges_.emplace_back(Edge(std::move(n.value())));
        }
    }

    return edges_;
}

std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> DSRGraph::get_edges(uint64_t id) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<Node> n = get_node(id);
    if (n.has_value())
    {
        return n->fano();
    }
    return std::nullopt;
}


/////////////////////////////////////////////////
///// Utils
/////////////////////////////////////////////////

std::map<uint64_t, DSR::Node> DSRGraph::getCopy() const
{
    std::map<uint64_t, Node> mymap;
    std::shared_lock<std::shared_mutex> lock(_mutex);

    for (auto &[key, val] : nodes)
        mymap.emplace(key, Node(val.read_reg()));

    return mymap;
}

//////////////////////////////////////////////////////////////////////////////
/////  CORE
//////////////////////////////////////////////////////////////////////////////

std::optional<CRDTNode> DSRGraph::get_(uint64_t id)
{
    auto it = nodes.find(id);
    if (it != nodes.end() and !it->second.empty())
    {
        return std::make_optional(it->second.read_reg());
    }
    return {};
}

std::optional<std::int32_t> DSRGraph::get_node_level(const Node &n)
{
    return get_attrib_by_name<level_att>(n);
}

std::optional<uint64_t> DSRGraph::get_parent_id(const Node &n)
{
    return get_attrib_by_name<parent_att>(n);
}

std::optional<DSR::Node> DSRGraph::get_parent_node(const Node &n)
{
    auto p = get_attrib_by_name<parent_att>(n);
    if (p.has_value())
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        auto tmp = graph->get_(p.value());
        if (tmp.has_value()) return Node(tmp.value());
    }
    return {};
}


std::string DSRGraph::get_node_type(Node &n)
{
    return n.type();
}

//////////////////////////////////////////////////////////////////////////////////////////////

std::optional<uint64_t> DSRGraph::get_id_from_name(const std::string &name)
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    auto v = name_map.find(name);
    if (v != name_map.end()) return v->second;
    return {};
}

std::optional<std::string> DSRGraph::get_name_from_id(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    auto v = id_map.find(id);
    if (v != id_map.end()) return v->second;
    return {};
}

size_t DSRGraph::size() {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return nodes.size();
}


bool DSRGraph::empty(const uint64_t &id)
{
    auto it = nodes.find(id);
    if (it != nodes.end()) {
        return it->second.empty();
    } else
        return false;
}

//////////////////////////////////////////////////
///// PRIVATE COPY
/////////////////////////////////////////////////

DSRGraph::DSRGraph(const DSRGraph &G) : agent_id(G.agent_id), copy(true), tp(1), generator(G.agent_id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    nodes = G.nodes;
    utils = std::make_unique<Utilities>(this);
    id_map = G.id_map;
    deleted = G.deleted;
    name_map = G.name_map;
    edges = G.edges;
    edgeType = G.edgeType;
    nodeType = G.nodeType;
    same_host = G.same_host;
}

std::unique_ptr<DSRGraph> DSRGraph::G_copy()
{
    return std::unique_ptr<DSRGraph>(new DSRGraph(*this));
}

bool DSRGraph::is_copy() const
{
    return copy;
}
inline uint64_t DSR::DSRGraph::get_agent_id() const
{
    return config.agent_id;
};
inline std::string DSR::DSRGraph::get_agent_name() const
{
    return config.agent_name;
};
void DSR::DSRGraph::print()
{
    utils->print();
};
void DSR::DSRGraph::print_edge(const Edge &edge)
{
    utils->print_edge(edge);
};
void DSR::DSRGraph::print_node(const Node &node)
{
    utils->print_node(node);
};
void DSR::DSRGraph::print_node(uint64_t id)
{
    utils->print_node(id);
};
void DSR::DSRGraph::print_RT(uint64_t root) const
{
    utils->print_RT(root);
};
void DSR::DSRGraph::write_to_json_file(const std::string &file) const
{
    utils->write_to_json_file(file, skip_node_content);
};
void DSR::DSRGraph::read_from_json_file(const std::string &file)
{
    utils->read_from_json_file(
        file,
        [&](const Node &node) -> std::optional<uint64_t>
        {
            bool r = false;
            {
                std::unique_lock<std::shared_mutex> lock(_mutex);
                if (auto t1 = id_map.find(node.id()) == id_map.end(), t2 = name_map.find(node.name()) == name_map.end();
                    t1 and t2)
                {
                    std::tie(r, std::ignore) = insert_node_(user_node_to_crdt(node));
                }
                else
                {
                    if (!t1 and t2)
                        throw std::runtime_error((std::string("Cannot insert node in G, a node with the same id (" +
                                                              std::to_string(node.id()) + ") already exists ") +
                                                  __FILE__ + " " + " " + std::to_string(__LINE__))
                                                     .data());
                    else if (t1 and !t2)
                        throw std::runtime_error((std::string("Cannot insert node in G, a node with the same name (" +
                                                              node.name() + ") already exists ") +
                                                  __FILE__ + " " + " " + std::to_string(__LINE__))
                                                     .data());
                    else
                        throw std::runtime_error(
                            (std::string("Cannot insert node in G, a node with the same name (" + node.name() +
                                         ") and same id (" + std::to_string(node.id()) + ") already exists ") +
                             __FILE__ + " " + " " + std::to_string(__LINE__))
                                .data());
                }
            }
            if (r)
            {
                return node.id();
            }
            return {};
        });
};
void DSR::DSRGraph::reset()
{
    graph->reset();
}
std::optional<Node> DSR::DSRGraph::get_node_root()
{
    return get_node("world");
};