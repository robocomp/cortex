#include "dsr/api/dsr_core_api.h"

#include "dsr/api/dsr_api.h"
#include "dsr/api/dsr_signal_info.h"
#include "dsr/api/dsr_transport.h"
#include "dsr/api/dsr_core_api.h"
#include "dsr/core/id_generator.h"
#include "dsr/core/types/translator.h"

#include <memory>

using namespace DSR;

Graph::Graph(CortexConfig &cfg) : config(cfg)
{
    transport = Transport::create(std::move(config.comm));
    transport->start_topics_publishing(this, false);

    qDebug() << __FUNCTION__ << "Constructor finished OK";
}


auto Graph::init(std::function<void(std::string)> read_from_json_file) -> void
{

    if (config.load_file.has_value())
    {
        read_from_json_file(*config.load_file);
        transport->start_fullgraph_server(this);
        transport->start_topics_subcription(this, false);
    }
    else
    {
        transport->start_topics_subcription(this, false);
        if (!config.init_empty)
        {
            auto [response, repeated] =
                transport->start_fullgraph_request(this);  // for agents that want to request the graph for other agent

            if (!response)
            {
                transport->stop();  // Remove a Participant and all associated publishers and subscribers.

                if (repeated)
                {
                    qFatal("%s", (std::string("There is already an agent connected with the id: ") +
                                  std::to_string(config.agent_id))
                                     .c_str());
                }
                else
                {
                    qFatal("DSRGraph aborting: could not get DSR from the network after timeout");
                }
            }
        }
    }
}

Graph::~Graph() {}

//////////////////////////////////////////////////////
///  Graph API
//////////////////////////////////////////////////////

//////////////////////////////////////////////////////
///  CONVENIENCE METHODS
//////////////////////////////////////////////////////

auto Graph::size() const -> size_t
{
    std::shared_lock<std::shared_mutex> lock(_mutex_data);
    return nodes.size();
}

auto Graph::empty(uint64_t id) const -> bool
{
    std::shared_lock<std::shared_mutex> lock(_mutex_data);
    auto it = nodes.find(id);
    return (it != nodes.end()) ? it->second.empty() : false;
}

auto Graph::get_copy() const -> std::map<uint64_t, Node>
{
    std::map<uint64_t, Node> data;
    std::shared_lock<std::shared_mutex> lock(_mutex_data);

    for (auto &[key, val] : nodes)
        data.emplace(key, Node(val.read_reg()));

    return data;
}

auto Graph::copy_map() const -> std::map<uint64_t, NodeInfoTuple>
{
    std::map<uint64_t, NodeInfoTuple> map;
    std::shared_lock<std::shared_mutex> lock(_mutex_data);

    for (auto &[key, val] : nodes)
        map.emplace(key, std::tuple {val, key, config.agent_id });

    return map;
}

auto Graph::get_agent_id() const -> uint64_t
{
    return config.agent_id;
}

void Graph::reset()
{
    transport.reset();

    nodes.clear();
    deleted.clear();
    name_map.clear();
    id_map.clear();
    edges.clear();
    edgeType.clear();
    nodeType.clear();
    to_edges.clear();
}

auto Graph::get_config() -> CortexConfig&
{
    return config;
}

auto Graph::get_ignored_attributes() -> const std::unordered_set<std::string_view>& 
{
    return ignored_attributes;
}

void Graph::update_maps_node_delete(uint64_t id, const std::optional<CRDTNode> &n)
{
    nodes.erase(id);
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);
    if (id_map.contains(id))
    {
        name_map.erase(id_map.at(id));
        id_map.erase(id);
    }
    deleted.insert(id);
    to_edges.erase(id);

    if (n.has_value())
    {
        if (nodeType.contains(n->type()))
        {
            nodeType.at(n->type()).erase(id);
            if (nodeType.at(n->type()).empty()) nodeType.erase(n->type());
        }
        for (const auto &[k, v] : n->fano())
        {
            if (auto tuple = std::pair{id, v.read_reg().to()}; edges.contains(tuple))
            {
                edges.at(tuple).erase(k.second);
                if (edges.at(tuple).empty()) edges.erase(tuple);
            }
            if (auto tuple = std::pair{id, k.first}; edgeType.contains(k.second))
            {
                edgeType.at(k.second).erase(tuple);
                if (edgeType.at(k.second).empty()) edgeType.erase(k.second);
            }
            if (auto tuple = std::pair{id, k.second}; to_edges.contains(k.first))
            {
                to_edges.at(k.first).erase(tuple);
                if (to_edges.at(k.first).empty()) to_edges.erase(k.first);
            }
        }
    }
}

void Graph::update_maps_node_insert(uint64_t id, const CRDTNode &n)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);
    name_map[n.name()] = id;
    id_map[id] = n.name();
    nodeType[n.type()].emplace(id);
    for (const auto &[k, v] : n.fano())
    {
        edges[{id, k.first}].insert(k.second);
        edgeType[k.second].insert({id, k.first});
        to_edges[k.first].insert({id, k.second});
    }
}

void Graph::update_maps_edge_delete(uint64_t from, uint64_t to, const std::string &key)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);
    // if key is empty we delete all edges to the node from
    if (key.empty())
    {
        edges.erase({from, to});

        if (edgeType.contains(key))
        {
            edgeType.at(key).erase({from, to});
            if (edgeType.at(key).empty()) edgeType.erase(key);
        }

        if (to_edges.contains(to))
        {
            auto &set = to_edges.at(to);
            auto it = set.begin();
            while (it != set.end())
            {
                if (it->first == from) it = set.erase(it);
                else it++;
            }
            if (set.empty()) to_edges.erase(to);
        }
    }
    else
    {
        if (auto tuple = std::pair{from, to}; edges.contains(tuple))
        {
            edges.at(tuple).erase(key);
            edges.erase({from, to});
        }

        if (to_edges.contains(to))
        {
            to_edges.at(to).erase({from, key});
            if (to_edges.at(to).empty()) to_edges.erase(to);
        }

        if (edgeType.contains(key))
        {
            edgeType.at(key).erase({from, to});
            if (edgeType.at(key).empty()) edgeType.erase(key);
        }
    }
}

void Graph::update_maps_edge_insert(uint64_t from, uint64_t to, const std::string &key)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);

    edges[{from, to}].insert(key);
    to_edges[to].insert({from, key});
    edgeType[key].insert({from, to});
}

//////////////////////////////////////////////////////////////////////////
// Non-blocking graph operations
//////////////////////////////////////////////////////////////////////////

auto Graph::get_(uint64_t id) -> std::optional<CRDTNode>
{
    std::shared_lock<std::shared_mutex> lock(_mutex_data);
    auto it = nodes.find(id);
    if (it != nodes.end() and !it->second.empty())
    {
        return std::make_optional(it->second.read_reg());
    }
    return {};
}

auto Graph::get_edge_(uint64_t from, uint64_t to, const std::string &key) -> std::optional<CRDTEdge>
{
    std::shared_lock<std::shared_mutex> lock(_mutex_data);
    if (nodes.contains(from) && nodes.contains(to))
    {
        auto n = get_(from);
        if (n.has_value())
        {
            auto edge = n.value().fano().find({to, key});
            if (edge != n.value().fano().end())
            {
                return edge->second.read_reg();
            }
        }
    }
    return {};
}

auto Graph::insert_node_(CRDTNode &&node) -> std::tuple<bool, std::optional<NodeInfoTuple>>
{
    if (deleted.find(node.id()) == deleted.end())
    {
        if (auto it = nodes.find(node.id());
            it != nodes.end() and not it->second.empty() and it->second.read_reg() == node)
        {
            return {true, {}};
        }

        update_maps_node_insert(node.id(), node);
        auto delta = nodes[node.id()].write(std::move(node));

        return {true, CRDTNode_to_IDL(config.agent_id, node.id(), delta)};
    }
    return {false, {}};
}

auto Graph::update_node_(CRDTNode &&node) -> std::tuple<bool, std::optional<NodeAttributeVecTuple>>
{

    if (deleted.find(node.id()) == deleted.end())
    {
        if (nodes.contains(node.id()) and !nodes.at(node.id()).empty())
        {

            NodeAttributeVecTuple atts_deltas;
            auto &iter = nodes.at(node.id()).read_reg().attrs();
            // New attributes and updates.
            for (auto &[k, att] : node.attrs())
            {
                if (!iter.contains(k))
                {
                    iter.emplace(k, mvreg<CRDTAttribute>());
                }
                if (iter.at(k).empty() or att.read_reg() != iter.at(k).read_reg())
                {
                    auto delta = iter.at(k).write(std::move(att.read_reg()));
                    atts_deltas.emplace_back(CRDTNodeAttr_to_IDL(config.agent_id, node.id(), node.id(), k, delta));
                }
            }
            // Remove old attributes.
            auto it_a = iter.begin();
            while (it_a != iter.end())
            {
                const std::string &k = it_a->first;
                if (ignored_attributes.contains(k))
                {
                    it_a = iter.erase(it_a);
                }
                else if (!node.attrs().contains(k))
                {
                    auto delta = iter.at(k).reset();
                    it_a = iter.erase(it_a);
                    atts_deltas.emplace_back(CRDTNodeAttr_to_IDL(node.agent_id(), node.id(), node.id(), k, delta));
                }
                else
                {
                    it_a++;
                }
            }

            return {true, std::move(atts_deltas)};
        }
    }

    return {false, {}};
}

auto Graph::delete_node_(uint64_t id) -> std::tuple<bool, std::vector<std::tuple<uint64_t, uint64_t, std::string>>,
                                                    std::optional<NodeInfoTuple>, std::vector<EdgeInfoTuple>>
{

    std::vector<std::tuple<uint64_t, uint64_t, std::string>> deleted_edges;
    std::vector<EdgeInfoTuple> delta_vec;

    // Get and remove node.
    auto node = get_(id);
    if (!node.has_value()) return make_tuple(false, deleted_edges, std::nullopt, delta_vec);
    // Delete all edges from this node.
    for (const auto &v : node.value().fano())
    {
        deleted_edges.emplace_back(make_tuple(id, v.first.first, v.first.second));
    }
    // Get remove delta.
    auto delta = nodes[id].reset();
    NodeInfoTuple delta_remove = CRDTNode_to_IDL(config.agent_id, id, delta);
    update_maps_node_delete(id, node.value());
    // search and remove edges.
    // For each node check if there is an edge to remove.
    // TODO: use to_edges.
    for (auto &[k, v] : nodes)
    {
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        if (!edges.contains({k, id})) continue;
        // Remove all edges between them
        auto &visited_node = v.read_reg();
        for (const auto &key : edges.at({k, id}))
        {
            auto delta_fano = visited_node.fano().at({id, key}).reset();
            delta_vec.emplace_back(CRDTEdge_to_IDL(config.agent_id, k, id, key, delta_fano));
            visited_node.fano().erase({id, key});
            deleted_edges.emplace_back(make_tuple(visited_node.id(), id, key));
        }
        lck_cache.unlock();
        // Remove all from cache
        update_maps_edge_delete(k, id);
    }

    return make_tuple(true, std::move(deleted_edges), std::move(delta_remove), std::move(delta_vec));
}

auto Graph::delete_edge_(uint64_t from, uint64_t to, const std::string &key) -> std::optional<EdgeInfoTuple>
{
    if (nodes.contains(from))
    {
        auto &node = nodes.at(from).read_reg();
        if (node.fano().contains({to, key}))
        {
            auto delta = node.fano().at({to, key}).reset();
            node.fano().erase({to, key});
            update_maps_edge_delete(from, to, key);
            return CRDTEdge_to_IDL(config.agent_id, from, to, key, delta);
        }
    }
    return {};
}

auto Graph::insert_or_assign_edge_(CRDTEdge &&attrs, uint64_t from, uint64_t to)
    -> std::tuple<bool, std::optional<EdgeInfoTuple>, std::optional<EdgeAttributeVecTuple>>
{

    std::optional<EdgeInfoTuple> delta_edge;
    std::optional<EdgeAttributeVecTuple> delta_attrs;

    if (nodes.contains(from))
    {
        auto &node = nodes.at(from).read_reg();
        // check if we are creating an edge or we are updating it.
        // Update
        if (node.fano().contains({to, attrs.type()}))
        {
            EdgeAttributeVecTuple atts_deltas;
            auto iter = nodes.at(from).read_reg().fano().find({attrs.to(), attrs.type()});
            auto end = nodes.at(from).read_reg().fano().end();
            if (iter != end)
            {
                auto &iter_edge = iter->second.read_reg().attrs();
                for (auto &[k, att] : attrs.attrs())
                {
                    // comparar igualdad o inexistencia
                    if (!iter_edge.contains(k))
                    {
                        iter_edge.emplace(k, mvreg<CRDTAttribute>());
                    }
                    if (iter_edge.at(k).empty() or att.read_reg() != iter_edge.at(k).read_reg())
                    {
                        auto delta = iter_edge.at(k).write(std::move(att.read_reg()));
                        atts_deltas.emplace_back(
                            CRDTEdgeAttr_to_IDL(config.agent_id, from, from, to, attrs.type(), k, delta));
                    }
                }
                auto it = iter_edge.begin();
                while (it != iter_edge.end())
                {
                    if (!attrs.attrs().contains(it->first))
                    {
                        std::string att = it->first;
                        auto delta = iter_edge.at(it->first).reset();
                        it = iter_edge.erase(it);
                        atts_deltas.emplace_back(
                            CRDTEdgeAttr_to_IDL(config.agent_id, from, from, to, attrs.type(), att, delta));
                    }
                    else
                    {
                        it++;
                    }
                }

                return {true, {}, std::move(atts_deltas)};
            }
        }
        else
        {  // Insert
            // node.fano().insert({{to, attrs.type()}, mvreg<CRDTEdge>()});
            std::string att_type = attrs.type();
            auto delta = node.fano()[{to, attrs.type()}].write(std::move(attrs));
            update_maps_edge_insert(from, to, att_type);
            return {true, CRDTEdge_to_IDL(config.agent_id, from, to, att_type, delta), {}};
        }
    }
    return {false, {}, {}};
}

//////////////////////////////////////////////////////////////////////////
// CRDT join operations
///////////////////////////////////////////////////////////////////////////
void Graph::join_delta_node(NodeInfoTuple &&join_node)
{

    std::optional<CRDTNode> maybe_deleted_node = {};
    try
    {
        bool signal = false, joined = false;
        auto &[node, timestamp, agent_id] = join_node;
        auto id = node.id;

        bool d_empty = node.empty();

        auto delete_unprocessed_deltas = [&]()
        {
            unprocessed_delta_node_att.erase(id);
            decltype(unprocessed_delta_edge_from)::node_type node_handle =
                std::move(unprocessed_delta_edge_from.extract(id));
            while (!node_handle.empty())
            {
                unprocessed_delta_edge_att.erase(
                    std::tuple{id, std::get<0>(node_handle.mapped()), std::get<1>(node_handle.mapped())});
                node_handle = std::move(unprocessed_delta_edge_from.extract(id));
            }
            std::erase_if(unprocessed_delta_edge_to, [&](auto &it) { return std::get<0>(it.second) == id; });
        };

        std::unordered_set<std::pair<uint64_t, std::string>, hash_tuple> map_new_to_edges = {};

        auto consume_unprocessed_deltas = [&]()
        {
            decltype(unprocessed_delta_node_att)::node_type node_handle_node_att =
                std::move(unprocessed_delta_node_att.extract(id));
            while (!node_handle_node_att.empty())
            {
                auto &[att_name, delta, timestamp_node_att] = node_handle_node_att.mapped();
                // std::cout << "node_att " << id<< ", " <<att_name << ", " <<
                // std::boolalpha << (timestamp < timestamp_node_att) <<
                // std::endl;
                if (timestamp < timestamp_node_att)
                {
                    process_delta_node_attr(id, att_name, std::move(delta));
                }
                node_handle_node_att = std::move(unprocessed_delta_node_att.extract(id));
            }

            decltype(unprocessed_delta_edge_from)::node_type node_handle_edge =
                std::move(unprocessed_delta_edge_from.extract(id));
            while (!node_handle_edge.empty())
            {
                auto &[to, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{id, to, type};
                // std::cout << "[OLD] Procesando delta edge almacenado
                // (unprocessed_delta_edge_from) " << id<< ", " <<to << ", "
                // <<type <<",
                // "<<std::boolalpha << (timestamp < timestamp_edge) <<
                // std::endl;
                if (timestamp < timestamp_edge)
                {
                    // TODO: este se debería hacer después de insertar el nodo?
                    if (process_delta_edge(id, to, type, std::move(delta)))
                        map_new_to_edges.emplace(std::pair<uint64_t, std::string>{to, type});
                }
                if (nodes.contains(id) and nodes.at(id).read_reg().fano().contains({to, type}))
                {
                    decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =
                        std::move(unprocessed_delta_edge_att.extract(att_key));
                    while (!node_handle_edge_att.empty())
                    {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        std::cout << "edge_att " << id << ", " << to << ", " << type << ", " << att_name << ", "
                                  << std::boolalpha << (timestamp < timestamp_edge) << std::endl;
                        if (timestamp < timestamp_edge_att)
                        {
                            process_delta_edge_attr(id, to, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = std::move(unprocessed_delta_edge_att.extract(att_key));
                    }
                }
                // TODO: Check
                std::erase_if(
                    unprocessed_delta_edge_to, [to = to, id = id, type = type](auto &it)
                    { return it.first == to && std::get<0>(it.second) == id && std::get<1>(it.second) == type; });
                node_handle_edge = std::move(unprocessed_delta_edge_from.extract(id));
            }

            // TODO: Check
            node_handle_edge = std::move(unprocessed_delta_edge_to.extract(id));
            while (!node_handle_edge.empty())
            {
                auto &[from, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{from, id, type};
                // std::cout << "[OLD] Procesando delta edge almacenado
                // (unprocessed_delta_edge_to) " << from << ", " <<id << ", "
                // <<type
                // <<", "<<std::boolalpha << (timestamp < timestamp_edge) <<
                // std::endl;
                if (timestamp < timestamp_edge)
                {
                    process_delta_edge(from, id, type, std::move(delta));
                }
                if (nodes.contains(from) and nodes.at(from).read_reg().fano().contains({id, type}))
                {
                    decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =
                        std::move(unprocessed_delta_edge_att.extract(att_key));
                    while (!node_handle_edge_att.empty())
                    {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        // std::cout << "edge_att " << from<< ", " <<id << ", "
                        // <<type <<",
                        // "<<att_name  << ", "<<std::boolalpha << (timestamp <
                        // timestamp_edge) << std::endl;
                        if (timestamp < timestamp_edge_att)
                        {
                            process_delta_edge_attr(from, id, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = std::move(unprocessed_delta_edge_att.extract(att_key));
                    }
                }

                node_handle_edge = std::move(unprocessed_delta_edge_to.extract(id));
            }
        };

        std::optional<std::unordered_set<std::pair<uint64_t, std::string>, hash_tuple>> cache_map_to_edges = {};
        {
            std::unique_lock<std::shared_mutex> lock(_mutex_data);
            if (!deleted.contains(id))
            {
                joined = true;
                maybe_deleted_node = (nodes[id].empty()) ? std::nullopt : std::make_optional(nodes.at(id).read_reg());
                nodes[id].join(std::move(crdt_delta));
                if (nodes.at(id).empty() or d_empty)
                {
                    nodes.erase(id);
                    // maybe_deleted_node = (nodes[id].empty()) ? std::nullopt :
                    // std::make_optional(nodes.at(id).read_reg()); //This is
                    // weird.
                    cache_map_to_edges = to_edges[id];
                    update_maps_node_delete(id, maybe_deleted_node);

                    delete_unprocessed_deltas();
                }
                else
                {
                    signal = true;
                    update_maps_node_insert(id, nodes.at(id).read_reg());
                    // std::cout << "INSERTANDO NODO " << id << std::endl;
                    consume_unprocessed_deltas();
                }
            }
            else
            {
                delete_unprocessed_deltas();
            }
        }

        if (joined)
        {
            if (signal)
            {
                emit config.dsr->update_node_signal(id, nodes.at(id).read_reg().type(), SignalInfo{mvreg.agent_id()});
                for (const auto &[k, v] : nodes.at(id).read_reg().fano())
                {
                    // std::cout << "[JOIN NODE] add edge FROM: "<< id << ", "
                    // << k.first
                    // << ", " << k.second << std::endl;
                    emit config.dsr->update_edge_signal(id, k.first, k.second, SignalInfo{mvreg.agent_id()});
                }

                for (const auto &[k, v] : map_new_to_edges)
                {
                    // std::cout << "[JOIN NODE] add edge TO: "<< k << ", " <<
                    // id << ", "
                    // << v << std::endl;
                    emit config.dsr->update_edge_signal(k, id, v, SignalInfo{mvreg.agent_id()});
                }
            }
            else
            {
                emit config.dsr->del_node_signal(id, SignalInfo{mvreg.agent_id()});
                if (maybe_deleted_node.has_value())
                {
                    for (const auto &node : maybe_deleted_node->fano())
                    {
                        // std::cout << "[JOIN NODE] delete edge FROM: "<<
                        // node.second.read_reg().from() << ", " <<
                        // node.second.read_reg().to() << ", " <<
                        // node.second.read_reg().type() << std::endl;
                        emit config.dsr->del_edge_signal(node.second.read_reg().from(), node.second.read_reg().to(),
                                                         node.second.read_reg().type(), SignalInfo{mvreg.agent_id()});
                    }
                }

                for (const auto &[from, type] : cache_map_to_edges.value())
                {
                    // std::cout << "[JOIN NODE] delete edge TO: "<< from << ",
                    // " << id <<
                    // ", " << type << std::endl;
                    emit config.dsr->del_edge_signal(from, id, type, SignalInfo{mvreg.agent_id()});
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }
}

void Graph::join_delta_edge(EdgeInfoTuple &&mvreg)
{
    try
    {
        bool signal = false, joined = false;
        auto from = mvreg.id();
        auto to = mvreg.to();
        std::string type = mvreg.type();

        uint64_t timestamp = mvreg.timestamp();

        auto delete_unprocessed_deltas = [&]()
        {
            unprocessed_delta_edge_from.erase(from);
            unprocessed_delta_edge_att.erase(std::tuple{from, to, type});
            std::erase_if(unprocessed_delta_edge_to,
                          [&](auto &it) { return it.first == to && std::get<0>(it.second) == from; });
        };

        // Consumes all delta attributes and deletes a possible previous delta
        // from unprocessed map.
        auto consume_unprocessed_deltas = [&]()
        {
            auto att_key = std::tuple{from, to, type};
            decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =
                std::move(unprocessed_delta_edge_att.extract(att_key));
            while (!node_handle_edge_att.empty())
            {
                auto &[att_name, delta, timestamp_delta_edge] = node_handle_edge_att.mapped();
                if (timestamp < timestamp_delta_edge)
                {
                    process_delta_edge_attr(from, to, type, att_name, std::move(delta));
                }
                node_handle_edge_att = std::move(unprocessed_delta_edge_att.extract(att_key));
            }

            // auto [begin, end] = unprocessed_delta_edge.equal_range(from);
            std::erase_if(unprocessed_delta_edge_to,
                          [&](auto &it) {
                              return it.first == to && std::get<0>(it.second) == from && std::get<1>(it.second) == type;
                          });
            std::erase_if(unprocessed_delta_edge_from,
                          [&](auto &it) { return std::get<0>(it.second) == to && std::get<1>(it.second) == type; });
        };

        auto crdt_delta = IDLEdge_to_CRDT(std::move(mvreg));
        {
            std::unique_lock<std::shared_mutex> lock(_mutex_data);
            // Check if the node where we are joining the edge exist.
            bool cfrom{nodes.contains(from)}, cto{nodes.contains(to)};
            bool dfrom{deleted.contains(from)}, dto{deleted.contains(to)};
            if (cfrom and cto)
            {
                joined = true;
                signal = process_delta_edge(from, to, type, std::move(crdt_delta));
                if (signal)
                {
                    consume_unprocessed_deltas();
                }
                else
                {
                    delete_unprocessed_deltas();
                }
            }
            else if (!dfrom and !dto)
            {
                // We should receive the node later.
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_edge_from.equal_range(from); begin != end; ++begin)
                {  // There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == to && std::get<1>(begin->second) == type)
                    {
                        std::get<2>(begin->second).join(std::move(crdt_delta));
                        std::cout << "JOIN_DELTA_EDGE ID(" << from << ", " << to << ", " << type << ") JOIN UNPROCESSED"
                                  << std::endl;
                        find = true;
                        break;
                    }
                }

                for (auto [begin, end] = unprocessed_delta_edge_from.equal_range(to); begin != end; ++begin)
                {  // There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == from && std::get<1>(begin->second) == type)
                    {
                        std::get<2>(begin->second).join(std::move(crdt_delta));
                        std::cout << "JOIN_DELTA_EDGE ID(" << from << ", " << to << ", " << type << ") JOIN UNPROCESSED"
                                  << std::endl;
                        find = true;
                        break;
                    }
                }

                if (!find)
                {
                    std::cout << "JOIN_DELTA_EDGE ID(" << from << ", " << to << ", " << type << ") INSERT UNPROCESSED ";
                    if (!cfrom)
                    {
                        std::cout << "No existe from (" << from << ") unprocessed_delta_edge_from" << std::endl;
                        unprocessed_delta_edge_from.emplace(from, std::tuple{to, type, crdt_delta, timestamp});
                    }
                    if (cfrom && !cto)
                    {
                        std::cout << "No existe to (" << to << ") unprocessed_delta_edge_to" << std::endl;
                        unprocessed_delta_edge_to.emplace(to, std::tuple{from, type, std::move(crdt_delta), timestamp});
                    }
                }
                else
                {
                    // We should sync the deleted_nodes set too...
                    std::cout << "TODO: Unhandled" << std::endl;
                }
            }
            else
            {
                std::cout << "ELIMINADO, BORRAR DE UNPROCESSED" << std::endl;
                auto node_deleted = [&](uint64_t id)
                {
                    unprocessed_delta_edge_from.erase(id);
                    unprocessed_delta_node_att.erase(id);
                    std::erase_if(unprocessed_delta_edge_to, [&](auto &it) { return std::get<0>(it.second) == id; });
                    std::erase_if(unprocessed_delta_edge_att, [&](auto &it) { return std::get<0>(it.first) == id; });
                };
                if (dfrom) node_deleted(from);
                if (dto) node_deleted(to);
            }
        }

        if (joined)
        {
            if (signal)
            {
                // std::cout << "[JOIN EDGE] add edge: "<< from << ", " << to <<
                // ", " << type << std::endl;
                emit config.dsr->update_edge_signal(from, to, type, SignalInfo{mvreg.agent_id()});
            }
            else
            {
                // std::cout << "[JOIN EDGE] delete edge: "<< from << ", " << to
                // << ", "
                // << type << std::endl;
                emit config.dsr->del_edge_signal(from, to, type, SignalInfo{mvreg.agent_id()});
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }
}

auto Graph::join_delta_node_attr(NodeAttributeVecTuple &&mvreg) -> std::optional<std::string>
{

    try
    {
        bool joined = false;
        auto id = mvreg.id();
        std::string att_name = mvreg.attr_name();
        uint64_t timestamp = mvreg.timestamp();

        auto crdt_delta = IDLNodeAttr_to_CRDT(std::move(mvreg));
        {
            std::unique_lock<std::shared_mutex> lock(_mutex_data);
            // Check if the node where we are joining the edge exist.
            if (nodes.contains(id))
            {
                joined = true;
                process_delta_node_attr(id, att_name, std::move(crdt_delta));
                std::erase_if(unprocessed_delta_node_att,
                              [&](auto &it) { return it.first == id && std::get<0>(it.second) == att_name; });
            }
            else if (!deleted.contains(id))
            {
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_node_att.equal_range(id); begin != end; ++begin)
                {  // There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == att_name)
                    {
                        std::get<1>(begin->second).join(std::move(crdt_delta));
                        find = true;
                        break;
                    }
                }
                if (!find)
                {
                    unprocessed_delta_node_att.emplace(id, std::tuple{att_name, std::move(crdt_delta), timestamp});
                }
            }
            else
            {
                unprocessed_delta_edge_from.erase(id);
                std::erase_if(unprocessed_delta_edge_to, [&](auto &it) { return std::get<0>(it.second) == id; });
                unprocessed_delta_node_att.erase(id);
                std::erase_if(unprocessed_delta_edge_att, [&](auto &it) { return std::get<0>(it.first) == id; });
            }
        }

        if (joined)
        {
            return att_name;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }

    return std::nullopt;
}
auto Graph::join_delta_edge_attr(EdgeAttributeVecTuple &&mvreg) -> std::optional<std::string>
{
    try
    {
        bool joined = false;
        auto from = mvreg.id();
        auto to = mvreg.to();
        std::string type = mvreg.type();
        std::string att_name = mvreg.attr_name();
        uint64_t timestamp = mvreg.timestamp();

        auto crdt_delta = IDLEdgeAttr_to_CRDT(std::move(mvreg));
        {
            std::unique_lock<std::shared_mutex> lock(_mutex_data);
            // Check if the node where we are joining the edge exist.
            if (nodes.contains(from) and nodes.at(from).read_reg().fano().contains({to, type}))
            {
                joined = true;
                process_delta_edge_attr(from, to, type, att_name, std::move(crdt_delta));
                std::erase_if(unprocessed_delta_edge_att,
                              [&](auto &it) {
                                  return it.first == std::tuple{from, to, type} && std::get<0>(it.second) == att_name;
                              });
            }
            else if (!deleted.contains(from))
            {
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_edge_att.equal_range(std::tuple{from, to, type});
                     begin != end; ++begin)
                {  // There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == att_name)
                    {
                        std::get<1>(begin->second).join(std::move(crdt_delta));
                        find = true;
                        break;
                    }
                }
                if (!find)
                {
                    unprocessed_delta_edge_att.emplace(std::tuple{from, to, type},
                                                       std::tuple{att_name, std::move(crdt_delta), timestamp});
                }
            }
            else
            {  // If the node is deleted
                unprocessed_delta_edge_from.erase(from);
                std::erase_if(unprocessed_delta_edge_to, [&](auto &it) { return std::get<0>(it.second) == from; });
                unprocessed_delta_node_att.erase(from);
                std::erase_if(unprocessed_delta_edge_att, [&](auto &it) { return std::get<0>(it.first) == from; });
            }
        }

        if (joined)
        {
            return att_name;
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }

    return std::nullopt;
}

void Graph::join_full_graph(GraphInfoTuple &&full_graph)
{

    std::vector<std::tuple<bool, uint64_t, std::string, std::optional<CRDTNode>>> updates;

    uint64_t id{0}, timestamp{0};
    uint32_t agent_id_ch{0};
    auto delete_unprocessed_deltas = [&]()
    {
        unprocessed_delta_node_att.erase(id);
        decltype(unprocessed_delta_edge_from)::node_type node_handle =
            std::move(unprocessed_delta_edge_from.extract(id));
        while (!node_handle.empty())
        {
            unprocessed_delta_edge_att.erase(
                std::tuple{id, std::get<0>(node_handle.mapped()), std::get<1>(node_handle.mapped())});
            node_handle = std::move(unprocessed_delta_edge_from.extract(id));
        }
        std::erase_if(unprocessed_delta_edge_to, [&](auto &it) { return std::get<0>(it.second) == id; });
    };

    auto consume_unprocessed_deltas = [&]()
    {
        decltype(unprocessed_delta_node_att)::node_type node_handle_node_att =
            std::move(unprocessed_delta_node_att.extract(id));
        while (!node_handle_node_att.empty())
        {
            auto &[att_name, delta, timestamp_node_att] = node_handle_node_att.mapped();
            if (timestamp < timestamp_node_att)
            {
                process_delta_node_attr(id, att_name, std::move(delta));
            }
            node_handle_node_att = std::move(unprocessed_delta_node_att.extract(id));
        }

        decltype(unprocessed_delta_edge_from)::node_type node_handle_edge =
            std::move(unprocessed_delta_edge_from.extract(id));
        while (!node_handle_edge.empty())
        {
            auto &[to, type, delta, timestamp_edge] = node_handle_edge.mapped();
            auto att_key = std::tuple{id, to, type};
            // std::cout << "Procesando delta edge almacenado
            // (unprocessed_delta_edge_from) " << id<< ", " <<to << ", " <<type
            // <<",
            // "<<std::boolalpha << (timestamp < timestamp_edge) << std::endl;
            if (timestamp < timestamp_edge)
            {
                // TODO: este se debería hacer después de insertar el nodo?
                process_delta_edge(id, to, type, std::move(delta));
            }
            if (nodes.contains(id) and nodes.at(id).read_reg().fano().contains({to, type}))
            {
                decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =
                    std::move(unprocessed_delta_edge_att.extract(att_key));
                while (!node_handle_edge_att.empty())
                {
                    auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                    std::cout << "edge_att " << id << ", " << to << ", " << type << ", " << att_name << ", "
                              << std::boolalpha << (timestamp < timestamp_edge) << std::endl;
                    if (timestamp < timestamp_edge_att)
                    {
                        process_delta_edge_attr(id, to, type, att_name, std::move(delta));
                    }
                    node_handle_edge_att = std::move(unprocessed_delta_edge_att.extract(att_key));
                }
            }
            // TODO: Check
            std::erase_if(unprocessed_delta_edge_to, [to = to, id = id, type = type](auto &it)
                          { return it.first == to && std::get<0>(it.second) == id && std::get<1>(it.second) == type; });
            node_handle_edge = std::move(unprocessed_delta_edge_from.extract(id));
        }

        // TODO: Check
        node_handle_edge = std::move(unprocessed_delta_edge_to.extract(id));
        while (!node_handle_edge.empty())
        {
            auto &[from, type, delta, timestamp_edge] = node_handle_edge.mapped();
            auto att_key = std::tuple{from, id, type};
            // std::cout << "Procesando delta edge almacenado
            // (unprocessed_delta_edge_to) " << from << ", " <<id << ", " <<type
            // <<",
            // "<<std::boolalpha << (timestamp < timestamp_edge) << std::endl;
            if (timestamp < timestamp_edge)
            {
                process_delta_edge(from, id, type, std::move(delta));
            }
            if (nodes.contains(from) and nodes.at(from).read_reg().fano().contains({id, type}))
            {
                decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =
                    std::move(unprocessed_delta_edge_att.extract(att_key));
                while (!node_handle_edge_att.empty())
                {
                    auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                    // std::cout << "edge_att " << from<< ", " <<id << ", "
                    // <<type <<",
                    // "<<att_name  << ", "<<std::boolalpha << (timestamp <
                    // timestamp_edge) << std::endl;
                    if (timestamp < timestamp_edge_att)
                    {
                        process_delta_edge_attr(from, id, type, att_name, std::move(delta));
                    }
                    node_handle_edge_att = std::move(unprocessed_delta_edge_att.extract(att_key));
                }
            }

            node_handle_edge = std::move(unprocessed_delta_edge_to.extract(id));
        }
    };

    {
        std::unique_lock<std::shared_mutex> lock(_mutex_data);

        for (auto &[k, val] : full_graph.m())
        {
            auto mv = IDLNode_to_CRDT(std::move(val));
            bool mv_empty = mv.empty();
            agent_id_ch = val.agent_id();
            std::optional<CRDTNode> nd = (nodes[k].empty()) ? std::nullopt : std::make_optional(nodes[k].read_reg());
            id = k;
            if (!deleted.contains(k))
            {
                nodes[k].join(std::move(mv));
                if (mv_empty or nodes.at(k).empty())
                {
                    update_maps_node_delete(k, nd);
                    updates.emplace_back(make_tuple(false, k, "", std::nullopt));
                    delete_unprocessed_deltas();
                }
                else
                {
                    update_maps_node_insert(k, nodes.at(k).read_reg());
                    updates.emplace_back(make_tuple(true, k, nodes.at(k).read_reg().type(), nd));
                    consume_unprocessed_deltas();
                }
            }
        }
    }
    for (auto &[signal, id, type, nd] : updates)
        if (signal)
        {
            // check what change is joined
            if (!nd.has_value() || nd->attrs() != nodes[id].read_reg().attrs())
            {
                emit config.dsr->update_node_signal(id, nodes[id].read_reg().type(), SignalInfo{agent_id_ch});
            }
            else if (nd.value() != nodes[id].read_reg())
            {
                auto iter = nodes[id].read_reg().fano();
                for (const auto &[k, v] : nd->fano())
                {
                    if (!iter.contains(k))
                        emit config.dsr->del_edge_signal(id, k.first, k.second, SignalInfo{agent_id_ch});
                }
                for (const auto &[k, v] : iter)
                {
                    if (auto it = nd->fano().find(k); it == nd->fano().end() or it->second != v)
                        emit config.dsr->update_edge_signal(id, k.first, k.second, SignalInfo{agent_id_ch});
                }
            }
        }
        else
        {
            emit config.dsr->del_node_signal(id, SignalInfo{agent_id_ch});
        }
}

auto Graph::process_delta_edge(uint64_t from, uint64_t to, const std::string &type, mvreg<CRDTEdge> &&delta) -> bool
{
    const bool d_empty = delta.empty();
    auto &node = nodes.at(from).read_reg();
    node.fano()[{to, type}].join(std::move(delta));
    if (d_empty or !node.fano().contains({to, type}))
    {  // Remove
        node.fano().erase({to, type});
        update_maps_edge_delete(from, to, type);
        return false;
    }
    else
    {  // Insert
        update_maps_edge_insert(from, to, type);
        return true;
    }
}

void Graph::process_delta_node_attr(uint64_t id, const std::string &att_name, mvreg<CRDTAttribute> &&attr)
{
    const bool d_empty = attr.empty();
    auto &n = nodes.at(id).read_reg();
    n.attrs()[att_name].join(std::move(attr));
    // Check if we are inserting or deleting.
    if (d_empty or not n.attrs().contains(att_name))
    {  // Remove
        n.attrs().erase(att_name);
    }
}

void Graph::process_delta_edge_attr(uint64_t from, uint64_t to, const std::string &type, const std::string &att_name,
                                    mvreg<CRDTAttribute> &&attr)
{
    const bool d_empty = attr.empty();
    auto &n = nodes.at(from).read_reg().fano().at({to, type}).read_reg();
    n.attrs()[att_name].join(std::move(attr));
    // Check if we are inserting or deleting.
    if (d_empty or !n.attrs().contains(att_name))
    {  // Remove
        n.attrs().erase(att_name);
    }
}
