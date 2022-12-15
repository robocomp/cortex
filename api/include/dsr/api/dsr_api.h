//
// Created by crivac on 17/01/19.
//

#pragma once

#include "dsr/api/dsr_agent_info_api.h"
#include "dsr/api/dsr_camera_api.h"
#include "dsr/api/dsr_core_api.h"
#include "dsr/api/dsr_eigen_defs.h"
#include "dsr/api/dsr_inner_eigen_api.h"
#include "dsr/api/dsr_rt_api.h"
#include "dsr/api/dsr_signal_info.h"
#include "dsr/api/dsr_utils.h"
#include "dsr/core/crdt/delta_crdt.h"
#include "dsr/core/id_generator.h"
#include "dsr/core/rtps/dsrparticipant.h"
#include "dsr/core/rtps/dsrpublisher.h"
#include "dsr/core/rtps/dsrsubscriber.h"
#include "dsr/core/topics/IDLGraphPubSubTypes.h"
#include "dsr/core/types/crdt_types.h"
#include "dsr/core/types/translator.h"
#include "dsr/core/types/type_checking/dsr_attr_name.h"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"
#include "dsr/core/types/user_types.h"
#include "dsr/core/utils.h"

#include <QtCore/QtCore>
#include <any>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace DSR
{

    /////////////////////////////////////////////////////////////////
    /// CRDT API
    /////////////////////////////////////////////////////////////////

    inline const std::vector<std::string> empty_string_vec = {};

    class DSRGraph : public QObject
    {
        friend RT_API;
        Q_OBJECT
    public:
        size_t size();
        DSRGraph(std::string name, uint32_t id, const std::string &dsr_input_file = std::string(),
                 bool all_same_host = true);
        DSRGraph(uint64_t root, std::string name, uint32_t id, const std::string &dsr_input_file = std::string(),
                 bool all_same_host = true);
        DSRGraph(CortexConfig cfg);

        ~DSRGraph() override;

        //////////////////////////////////////////////////////
        ///  Graph API
        //////////////////////////////////////////////////////

        // Utils
        bool empty(const uint64_t &id);            // TODO: this is in Graph
        std::map<uint64_t, Node> get_copy() const;
        [[deprecated("Use get_copy() instead")]]std::map<uint64_t, Node> getCopy() const;
        uint32_t get_agent_id() const;
        std::string get_agent_name() const;
        void reset();


        // TODO: add more apis or remove all
        std::unique_ptr<InnerEigenAPI> get_inner_eigen_api()
        {
            return std::make_unique<InnerEigenAPI>(this);
        };
        std::unique_ptr<RT_API> get_rt_api()
        {
            return std::make_unique<RT_API>(this);
        };
        std::unique_ptr<CameraAPI> get_camera_api(const DSR::Node &camera_node)
        {
            return std::make_unique<CameraAPI>(this, camera_node);
        };

        //////////////////////////////////////////////////////
        ///  Core API
        //////////////////////////////////////////////////////

        // Nodes
        std::optional<Node> get_node(const std::string &name);
        std::optional<Node> get_node(uint64_t id);
        template <typename No>
        std::optional<uint64_t> insert_node(No &&node) requires(std::is_same_v<std::remove_reference_t<No>, DSR::Node>);
        template <typename No>
        bool update_node(No &&node) requires(std::is_same_v<std::remove_cvref_t<No>, DSR::Node>);
        bool delete_node(const DSR::Node &node);
        bool delete_node(const std::string &name);
        bool delete_node(uint64_t id);

        // Edges
        template <typename Ed>
        bool insert_or_assign_edge(Ed &&attrs) requires(std::is_same_v<std::remove_cvref_t<Ed>, DSR::Edge>);
        std::optional<Edge> get_edge(const std::string &from, const std::string &to, const std::string &key);
        std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string &key);
        std::optional<Edge> get_edge(const Node &n, const std::string &to, const std::string &key);
        static std::optional<Edge> get_edge(const Node &n, uint64_t to, const std::string &key);
        bool delete_edge(const std::string &from, const std::string &t, const std::string &key);
        bool delete_edge(uint64_t from, uint64_t t, const std::string &key);
        /**CORE END**/

        //////////////////////////////////////////////////////
        ///  CONVENIENCE METHODS
        //////////////////////////////////////////////////////
        // Nodes
        std::optional<Node> get_node_root();
        std::vector<Node> get_nodes_by_type(const std::string &type);
        std::vector<Node> get_nodes_by_types(const std::vector<std::string> &types);
        std::optional<std::string> get_name_from_id(uint64_t id);
        std::optional<uint64_t> get_id_from_name(const std::string &name);
        std::optional<std::int32_t> get_node_level(const Node &n);
        std::optional<uint64_t> get_parent_id(const Node &n);
        std::optional<Node> get_parent_node(const Node &n);
        [[deprecated("Use node.type() instead")]] static std::string get_node_type(Node &n);

        // Edges
        std::vector<Edge> get_edges_by_type(const std::string &type);
        static std::vector<Edge> get_node_edges_by_type(const Node &node, const std::string &type);
        std::vector<Edge> get_edges_to_id(uint64_t id);
        std::optional<std::map<std::pair<uint64_t, std::string>, Edge>> get_edges(uint64_t id);

        template <typename name, typename Type>
        std::optional<uint64_t> get_attrib_timestamp(const Type &n) requires(node_or_edge<Type> and is_attr_name<name>)
        {
            auto &attrs = n.attrs();
            auto value = attrs.find(name::attr_name.data());
            if (value == attrs.end()) return {};
            else return value->second.timestamp();
        }

        template <typename Type>
        std::optional<uint64_t> get_attrib_timestamp_by_name(const Type &n,
                                                             const std::string &att_name) requires(node_or_edge<Type>)
        {
            auto &attrs = n.attrs();
            auto value = attrs.find(att_name);
            if (value == attrs.end()) return {};
            else return value->second.timestamp();
        }

        template <typename name, typename Type>
        inline std::optional<decltype(name::type)>
        get_attrib_by_name(const Type &n) requires(any_node_or_edge<Type> and is_attr_name<name>)
        {
            using name_type = std::remove_cv_t<
                unwrap_reference_wrapper_t<std::remove_reference_t<std::remove_cv_t<decltype(name::type)>>>>;

            auto &attrs = n.attrs();
            auto value = attrs.find(name::attr_name.data());
            if (value == attrs.end()) return {};

            const auto &av = [&]() -> const DSR::Attribute &
            {
                if constexpr (node_or_edge<Type>) return value->second;
                else return value->second.read_reg();
            }();

            if constexpr (std::is_same_v<name_type, float>) return av.fl();
            else if constexpr (std::is_same_v<name_type, double>) return av.dob();
            else if constexpr (std::is_same_v<name_type, std::string>) return av.str();
            else if constexpr (std::is_same_v<name_type, std::int32_t>) return av.dec();
            else if constexpr (std::is_same_v<name_type, std::uint32_t>) return av.uint();
            else if constexpr (std::is_same_v<name_type, std::uint64_t>) return av.uint64();
            else if constexpr (std::is_same_v<name_type, bool>) return av.bl();
            else if constexpr (std::is_same_v<name_type, std::vector<float>>) return av.float_vec();
            else if constexpr (std::is_same_v<name_type, std::vector<uint8_t>>) return av.byte_vec();
            else if constexpr (std::is_same_v<name_type, std::vector<uint64_t>>) return av.u64_vec();
            else if constexpr (std::is_same_v<name_type, std::array<float, 2>>) return av.vec2();
            else if constexpr (std::is_same_v<name_type, std::array<float, 3>>) return av.vec3();
            else if constexpr (std::is_same_v<name_type, std::array<float, 4>>) return av.vec4();
            else if constexpr (std::is_same_v<name_type, std::array<float, 6>>) return av.vec6();
            else
            {
                []<bool flag = false>()
                {
                    static_assert(flag, "Unreachable");
                }
                ();
            }
        }

        template <typename name>
        inline std::optional<std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(name::type)>>>
        get_attrib_by_name(uint64_t id) requires(is_attr_name<name>)
        {
            using ret_type = std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(name::type)>>;
            std::shared_lock<std::shared_mutex> lock(graph->_mutex_data);
            std::optional<CRDTNode> n = graph->get_(id);
            if (n.has_value())
            {
                auto tmp = get_attrib_by_name<name>(n.value());
                if (tmp.has_value())
                {
                    if constexpr (is_reference_wrapper<decltype(name::type)>::value)
                    {
                        return ret_type{tmp.value().get()};
                    }
                    else
                    {
                        return tmp;
                    }
                }
            }
            return {};
        }

        template <typename Type, typename... name>
        inline decltype(auto) get_attribs_by_name(Type n) requires((... && is_attr_name<name>)&&any_node_or_edge<Type>)
        {
            using ret_type =
                std::tuple<std::optional<std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(name::type)>>>...>;
            auto get_by_name = [&]<typename at>(at *dummy)
                -> std::optional<std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(at::type)>>>
            {
                auto tmp = get_attrib_by_name<at>(n);
                if (tmp.has_value())
                {
                    if constexpr (is_reference_wrapper<decltype(at::type)>::value)
                    {
                        return tmp.value().get();
                    }
                    else
                    {
                        return tmp;
                    }
                }
                else return {};
            };

            return ret_type(std::move(get_by_name(static_cast<name *>(nullptr)))...);
        }

        template <typename... name>
        inline decltype(auto) get_attribs_by_name(uint64_t id) requires((... && is_attr_name<name>))
        {
            using ret_type =
                std::tuple<std::optional<std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(name::type)>>>...>;
            std::shared_lock<std::shared_mutex> lock(graph->_mutex_data);
            std::optional<CRDTNode> node = graph->get_(id);
            if (node.has_value())
            {
                auto get_by_name = [&]<typename n>(n *dummy)
                    -> std::optional<std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(n::type)>>>
                {
                    auto tmp = get_attrib_by_name<n>(node.value());
                    if (tmp.has_value())
                    {
                        if constexpr (is_reference_wrapper<decltype(n::type)>::value)
                        {
                            return tmp.value().get();
                        }
                        else
                        {
                            return tmp;
                        }
                    }
                    else return {};
                };

                return ret_type(std::move(get_by_name(static_cast<name *>(nullptr)))...);
            }
            constexpr auto return_nullopt = []<typename n>(n *dummy)
            { return std::optional<std::remove_cvref_t<unwrap_reference_wrapper_t<decltype(n::type)>>>{}; };
            return ret_type(return_nullopt(static_cast<name *>(nullptr))...);
        }

        /**
         * LOCAL ATTRIBUTES MODIFICATION METHODS (for nodes and edges)
         **/
        template <typename name, typename Type, class Ta>
        inline void add_or_modify_attrib_local(Type &elem, Ta &&att_value) requires(
            any_node_or_edge<Type> and allowed_types<Ta> and is_attr_name<name> and valid_type<name, Ta>())
        {

            if constexpr (std::is_same_v<Type, Node> || std::is_same_v<Type, Edge>)
            {
                Attribute at(std::forward<Ta>(att_value), get_unix_timestamp(), config.agent_id);
                elem.attrs().insert_or_assign(name::attr_name.data(), at);
            }
            else
            {
                CRDTAttribute at;
                at.value(std::forward<Ta>(att_value));
                at.timestamp(get_unix_timestamp());
                if (elem.attrs().find(name::attr_name.data()) == elem.attrs().end())
                {
                    mvreg<CRDTAttribute> mv;
                    elem.attrs().insert(make_pair(name::attr_name, mv));
                }
                elem.attrs().at(name::attr_name.data()).write(at);
            }
        }

        template <typename Type, class Ta>
        inline void runtime_checked_add_or_modify_attrib_local(
            Type &elem, const std::string &att_name,
            Ta &&att_value) requires(any_node_or_edge<Type> and allowed_types<Ta>)
        {

            if (!attribute_types::check_type(att_name.data(), att_value))
            {
                throw std::runtime_error(std::string("Invalid type in attribute ") + att_name + " - " +
                                         typeid(att_value).name() + " in: " + __FILE__ " " + __FUNCTION__ + " " +
                                         std::to_string(__LINE__));
            }

            if constexpr (std::is_same_v<Type, Node> || std::is_same_v<Type, Edge>)
            {
                Attribute at(std::forward<Ta>(att_value), get_unix_timestamp(), config.agent_id);
                elem.attrs().insert_or_assign(att_name, at);
            }
            else
            {
                CRDTAttribute at;
                at.value(std::forward<Ta>(att_value));
                at.timestamp(get_unix_timestamp());
                if (elem.attrs().find(att_name) == elem.attrs().end())
                {
                    mvreg<CRDTAttribute> mv;
                    elem.attrs().insert(make_pair(att_name, mv));
                }
                elem.attrs().at(att_name).write(at);
            }
        }

        template <typename name, typename Type, typename Ta>
        bool
        add_attrib_local(Type &elem,
                         Ta &&att_value) requires(any_node_or_edge<Type> and allowed_types<Ta> and is_attr_name<name>)
        {
            if (elem.attrs().find(name::attr_name.data()) != elem.attrs().end()) return false;
            add_or_modify_attrib_local<name>(elem, std::forward<Ta>(att_value));
            return true;
        };

        template <typename Type, typename Ta>
        bool runtime_checked_add_attrib_local(Type &elem, const std::string &att_name,
                                              Ta &&att_value) requires(any_node_or_edge<Type> and allowed_types<Ta>)
        {
            if (elem.attrs().find(att_name) != elem.attrs().end()) return false;
            runtime_checked_add_or_modify_attrib_local(elem, att_name, std::forward<Ta>(att_value));
            return true;
        };

        template <typename name, typename Type>
        bool add_attrib_local(Type &elem, Attribute &attr) requires(any_node_or_edge<Type> and is_attr_name<name>)
        {
            if (elem.attrs().find(name::attr_name.data()) != elem.attrs().end()) return false;
            attr.timestamp(get_unix_timestamp());
            elem.attrs()[name::attr_name] = attr;
            return true;
        };

        template <typename Type>
        bool runtime_checked_add_attrib_local(Type &elem, const std::string &att_name,
                                              Attribute &attr) requires(any_node_or_edge<Type>)
        {
            // TODO: Check Attribute type? Or is checked when creating and Attribute
            if (elem.attrs().find(att_name) != elem.attrs().end()) return false;
            attr.timestamp(get_unix_timestamp());
            elem.attrs()[att_name] = attr;
            return true;
        };

        template <typename name, typename Type, typename Ta>
        bool modify_attrib_local(Type &elem, Ta &&att_value) requires(
            any_node_or_edge<Type> and allowed_types<Ta> and is_attr_name<name>)
        {
            if (elem.attrs().find(name::attr_name.data()) == elem.attrs().end()) return false;
            add_or_modify_attrib_local<name>(elem, std::forward<Ta>(att_value));
            return true;
        };

        template <typename Type, typename Ta>
        bool runtime_checked_modify_attrib_local(Type &elem, const std::string &att_name,
                                                 Ta &&att_value) requires(any_node_or_edge<Type> and allowed_types<Ta>)
        {
            if (elem.attrs().find(att_name) == elem.attrs().end()) return false;
            runtime_checked_add_or_modify_attrib_local(elem, att_name, std::forward<Ta>(att_value));
            return true;
        };

        template <typename name, typename Type>
        bool remove_attrib_local(Type &elem) requires(any_node_or_edge<Type> and is_attr_name<name>)
        {
            if (elem.attrs().find(name::attr_name.data()) == elem.attrs().end()) return false;
            elem.attrs().erase(name::attr_name.data());
            return true;
        }

        template <typename Type>
        bool remove_attrib_local(Type &elem, const std::string &att_name) requires(any_node_or_edge<Type>)
        {
            if (elem.attrs().find(att_name) == elem.attrs().end()) return false;
            elem.attrs().erase(att_name);
            return true;
        }

        //////////////////////////////////////////////////////
        ///  Attribute filters
        //////////////////////////////////////////////////////
        template <typename... Att>
        constexpr void set_ignored_attributes()
        {
            graph->set_ignored_attributes<Att...>();
        }

        /////////////////////////////////////////////////
        /// AUXILIARY IO SUB-API
        /////////////////////////////////////////////////
        void print();
        void print_edge(const Edge &edge);
        void print_node(const Node &node);
        void print_node(uint64_t id);
        void print_RT(uint64_t root) const;

        void write_to_json_file(const std::string &file,
                                const std::vector<std::string> &skip_node_content = empty_string_vec) const;

        void read_from_json_file(const std::string &file);

        //////////////////////////////////////////////////
        ///// PRIVATE COPY
        /////////////////////////////////////////////////
        std::unique_ptr<DSRGraph> G_copy();
        bool is_copy() const;

        //////////////////////////////////////////////////
        ///// Agents info
        /////////////////////////////////////////////////
        std::vector<std::string> get_connected_agents();

    private:
        DSRGraph(const DSRGraph &G);  // Private constructor for DSRCopy

        id_generator generator;
        CortexConfig config;

        std::shared_ptr<Graph> graph;
        std::unique_ptr<Utilities> utils;
        std::unordered_set<std::string_view> ignored_attributes;

    public:
    signals:
        void update_node_signal(uint64_t, const std::string &type, DSR::SignalInfo info = default_signal_info);
        void update_node_attr_signal(uint64_t id, const std::vector<std::string> &att_names,
                                     DSR::SignalInfo info = default_signal_info);

        void update_edge_signal(uint64_t from, uint64_t to, const std::string &type,
                                DSR::SignalInfo info = default_signal_info);
        void update_edge_attr_signal(uint64_t from, uint64_t to, const std::string &type,
                                     const std::vector<std::string> &att_name,
                                     DSR::SignalInfo info = default_signal_info);

        void del_edge_signal(uint64_t from, uint64_t to, const std::string &edge_tag,
                             DSR::SignalInfo info = default_signal_info);
        void del_node_signal(uint64_t id, DSR::SignalInfo info = default_signal_info);
    };
}  // namespace DSR
