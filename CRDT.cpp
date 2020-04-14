//
// Created by crivac on 5/02/19.
//



#include "CRDT.h"
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <QXmlSimpleReader>
#include <QXmlInputSource>
#include <QXmlDefaultHandler>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>




#include <fastrtps/subscriber/Subscriber.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <fastrtps/Domain.h>

using namespace CRDT;


// PUBLIC METHODS

CRDTGraph::CRDTGraph(int root, std::string name) {
    graph_root = root;
    agent_name = name;
    nodes = Nodes(graph_root);
    filter = "^(?!" + agent_name + "$).*$";

    int argc = 0;
    char *argv[0];
    //node = DataStorm::Node(argc, argv);
    work = true;

    // RTPS Create participant 
	auto [suc , participant_handle] = dsrparticipant.init();

    // General Topic
	// RTPS Initialize publisher
	dsrpub.init(participant_handle, "DSR", dsrparticipant.getDSRTopicName());
    dsrpub_graph_request.init(participant_handle, "DSR_GRAPH_REQUEST", dsrparticipant.getRequestTopicName());
    dsrpub_request_answer.init(participant_handle, "DSR_GRAPH_ANSWER", dsrparticipant.getAnswerTopicName());





    // General topic update
    //topic = std::make_shared < DataStorm::Topic < std::string, AworSet >> (node, "DSR");
    //topic->setWriterDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAll});

    // No filter for this topic
    //writer = std::make_shared < DataStorm::SingleKeyWriter < std::string, AworSet
    //>> (*topic.get(), agent_name);
}


CRDTGraph::~CRDTGraph() {
    //node.shutdown();
    //writer.reset();
    //topic.reset();
}


void CRDTGraph::add_edge(int from, int to, const std::string &label_) {
    try {
        if (in(from) && in(to)) {
            auto n = get(from);

            //map<std::string, AttribValue> attrs;

            EdgeAttribs ea;
            ea.label(label_);
            ea.from(from);
            ea.to(to);
            ea.attrs(vector<AttribValue>());
            
            n.fano().push_back(ea);
            insert_or_assign(from, n);
            emit update_edge_signal(from, to);
        } else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
    } catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


void CRDTGraph::add_edge_attrib(int from, int to, std::string att_name, CRDT::MTypes att_value) {
    try {
//        std::cout << "New edge from: "<<from<<" to: "<<to<<", with name: "<<att_name << std::endl;
        if (in(from)  && in(to)) {
            auto node = get(from);
            auto v = mtype_to_icevalue(att_value);
            std::cout << "Edge: "<<std::get<0>(v) << ", "<< std::get<1>(v) <<","<<std::get<2>(v)<<std::endl;
            
            AttribValue av;
            av.type(std::get<0>(v));
            av.value( std::get<1>(v));
            av.length(std::get<2>(v));
            av.key(att_name);

            auto fano_at = std::find_if( node.fano().begin(),  node.fano().end(), [&to](const auto val) { return val.to() == to;});
            if (fano_at != node.fano().end()) {
                fano_at->attrs().push_back(av);
                insert_or_assign(from, node);
                emit update_edge_signal(from, to);
            } else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
        }  
        else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
    } catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


void CRDTGraph::add_edge_attrib(int from, int to, std::string att_name, std::string att_type, std::string att_value, int length) {
    try {
        if (in(from) && in(to)) {
            auto node = get(from);

            AttribValue av;
            av.type(att_type);
            av.value( att_value);
            av.length(length);
            av.key(att_name);

            auto fano_at = std::find_if( node.fano().begin(),  node.fano().end(), [&to](const auto val) { return val.to() == to;});
            if (fano_at != node.fano().end()) {
                fano_at->attrs().push_back(av);
                insert_or_assign(from, node);
                emit update_edge_signal(from, to);
            } else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
        }  else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
    }
    catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


void CRDTGraph::add_edge_attribs(int from, int to, const std::vector<AttribValue> &att)
{
    try {
        if (in(from)  && in(to)) {
            auto node = get(from);
            for (auto v : att) {

                auto fano_at = std::find_if(node.fano().begin(), node.fano().end(),
                                            [&to](const auto val) { return val.to() == to; });
                if (fano_at != node.fano().end()) {
                    std::string key = v.key();
                    auto value = std::find_if(fano_at->attrs().begin(), fano_at->attrs().end(),
                                              [&key](const auto value) { return key == value.key(); });
                    if (value == fano_at->attrs().end())
                        fano_at->attrs().push_back(v);
                    else {
                        *value = v;
                    }
                    //node.fano().at(to).attrs().insert_or_assign(v);
                    insert_or_assign(from, node);
                    emit update_edge_signal(from, to);
                }

            }
        } else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
    }
    catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


/*
 * NODE METHODS
 */

void CRDTGraph::add_node_attrib(int id, std::string att_name, CRDT::MTypes att_value) {
    try {
        if (in(id)) {
            auto n = get(id);
            if (n.id() > 0) {
                auto v = mtype_to_icevalue(att_value);
                
                AttribValue av;
                av.type(std::get<0>(v));
                av.value( std::get<1>(v));
                av.length(std::get<2>(v));
                av.key(att_name);

                auto value  = std::find_if(n.attrs().begin(), n.attrs().end(), [&att_name](const auto value) { return att_name == value.key(); });
                if (value == n.attrs().end())
                    n.attrs().push_back(av);
                else {
                    *value = av;
                }
                //n.attrs().insert_or_assign(att_name, av);
                insert_or_assign(id, n);
                emit update_attrs_signal(id,  n.attrs()); // Viewer
            }
        } else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID "<<id<<" not found. Cant update. "<< std::endl;
    } catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


void CRDTGraph::add_node_attrib(int id, std::string att_name, std::string att_type, std::string att_value, int length) {
    try {
        auto n = get(id);
        if (n.id() > 0) {
            
            AttribValue av;
            av.type(att_type);
            av.value( att_value);
            av.length(length);
            av.key(att_name);

            auto value  = std::find_if(n.attrs().begin(), n.attrs().end(), [&att_name](const auto value) { return att_name == value.key(); });
            if (value == n.attrs().end())
                n.attrs().push_back(av);
            else {
                *value = av;
            }

            insert_or_assign(id, n);
            emit update_attrs_signal(id, n.attrs()); // Viewer
        }
} catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


void CRDTGraph::add_node_attribs(int id, const vector<AttribValue> &att) {
    try {
        if (in(id)) {
            auto n = get(id);
            if (n.id() > 0) {
                for (auto &v : att) {
                    std::string key = v.key();
                    auto value  = std::find_if(n.attrs().begin(), n.attrs().end(), [&key](const auto value) { return key == value.key(); });
                    if (value == n.attrs().end())
                        n.attrs().push_back(v);
                    else {
                        *value = v;
                    }
                    //n.attrs().insert_or_assign(k, v);
                }
                insert_or_assign(id, n);
                emit update_attrs_signal(id, n.attrs()); // Viewer
            }
        } else std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID "<<id<<" not found. Cant update. "<< std::endl;
    } catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};

}

void CRDTGraph::delete_node(int id) {
    std::lock_guard<std::mutex> lock(_mutex);
    std::cout << __FUNCTION__ <<". Node: "<<id<<std::endl;
    try {
        if (in(id)) {
            auto n = nodes[id].readAsList().back();

            for (auto v : n.fano()) { // Iterating over edge to this node
                std::cout << id << " -> " << v.to() << std::endl;
                emit del_edge_signal(id,v.to(),v.label());
            }

            auto map = nodes.getMap();
            for (auto &[k,v] : map) { // Iterating over edge from
                auto node_to_delete_edge = v.readAsList().back();
                auto value  = std::find_if(node_to_delete_edge.fano().begin(), node_to_delete_edge.fano().end(), [&id](const auto value) { return id == value.to(); });
                if (value != node_to_delete_edge.fano().end()) {
                    auto l =  value->label();
                    node_to_delete_edge.fano().erase(value);
                    auto delta = nodes[node_to_delete_edge.id()].add(node_to_delete_edge, node_to_delete_edge.id());
                    //writer->update(translateAwCRDTtoICE(node_to_delete_edge.id(), delta));
                    auto val = translateAwCRDTtoICE(node_to_delete_edge.id(), delta);
                    dsrpub.write(&val);

                    emit del_edge_signal(node_to_delete_edge.id(), id, l);
                }
            }
            auto delta = nodes[id].rmv(n); // If we compare only with ID (same)
//            auto delta = nodes[id].reset();
            std::cout<<"ID borrado: "<<nodes[id]<<std::endl;
            std::cout<<"Delta: "<<delta<<std::endl;
            emit del_node_signal(id);
            //writer->update(translateAwCRDTtoICE(id, delta));
            auto val = translateAwCRDTtoICE(id, delta);
            dsrpub.write(&val);
            //TODO: writer
        }
    }
    catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}


void CRDTGraph::delete_node(string name) {
    std::cout << __FUNCTION__ <<":" << __LINE__<<std::endl;

}

std::vector<EdgeAttribs> CRDTGraph::getEdges(int id) {
    if (in(id)) {
        auto n = get(id);
        if (n.id() > 0) return n.fano();
        else return std::vector<EdgeAttribs>();
    } else return std::vector<EdgeAttribs>();
}


Nodes CRDTGraph::get() {
    std::lock_guard<std::mutex> lock(_mutex);
    return nodes;
}

list<N> CRDTGraph::get_list() {
    std::lock_guard<std::mutex> lock(_mutex);
    list<N> mList;
    for (auto & kv : nodes.getMap())
        mList.push_back(kv.second.readAsList().back());
    return mList;
}


N CRDTGraph::get(int id) {
    std::lock_guard<std::mutex> lock(_mutex);
    try {
        if (in(id)) {
            list<N> node_list = nodes[id].readAsList();
            if (!node_list.empty()) return nodes[id].readAsList().back();
            else {
                Node n;
                n.type("empty");
                n.id(-1);
                return n;
            };
        }
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << "-> "<<id<<std::endl;
        Node n;
        n.type("error");
        n.id(-1);
        return n;
    }
}


AttribValue CRDTGraph::get_node_attrib_by_name(int id, const std::string &key) {
    try {
        auto attrs = get(id).attrs();
        auto value  = std::find_if(attrs.begin(), attrs.end(), [key](const auto value) { return key == value.key(); });
        if (value != attrs.end()) {
            return *value;
        }

        AttribValue av;
        av.type("unknown");
        av.value("unknow");
        av.key(key);
        av.length(0);

        return av;
    }
    catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << "-> "<<id<<std::endl;
        
        AttribValue av;
        av.type("unknown");
        av.value("unknow");
        av.length(0);
        
        return av;
    };
}

std::vector<AttribValue> CRDTGraph::get_node_attribs_crdt(int id) {
    return(get(id).attrs());
}

std::map<std::string, MTypes> CRDTGraph::get_node_attribs(int id) {
    std::map<std::string, MTypes> m;
    for (const auto v: get(id).attrs())
        m.insert(std::make_pair(v.key(), icevalue_to_mtypes(v.type(), v.value())));
    return m;

}


std::int32_t CRDTGraph::get_node_level(std::int32_t id){
    try {
        if(in(id)) return std::stoi(get_node_attrib_by_name(id, "level").value());
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl; };

    return -1;
}


std::string CRDTGraph::get_node_type(std::int32_t id) {
    try {
        if(in(id)) return get(id).type();
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
    return "error";
}


std::int32_t CRDTGraph::get_parent_id(std::int32_t id) {
    try {
        if(in(id)) return std::stoi(get_node_attrib_by_name(id, "parent").value());
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
    return -1;
}

int CRDTGraph::get_id_from_name(const std::string &tag) {
    if(tag == std::string()) return -1;
    for (auto & kv : nodes.getMap())
    {
        auto n = kv.second.readAsList().back();
        auto value = std::find_if(n.attrs().begin(), n.attrs().end(), [&tag](const auto value) {return value.key() == "imName"  && value.value() == tag;});
        if (value != n.attrs().end()) return n.id();
    }
    return -1;
}

EdgeAttribs CRDTGraph::get_edge_attrib(int from, int to) {
    try { if(in(from) && in(to)) {
            auto fano_at = std::find_if(get(from).fano().begin(), get(from).fano().end(),
                                        [&to](const auto val) { return val.to() == to; });
            if (fano_at != get(from).fano().end()) {
                return *fano_at;
            }
            else {
                EdgeAttribs ea;
                ea.label("error");
                return ea;
            }
        }
    }
     catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
    EdgeAttribs ea;
    ea.label("error");
    return ea;
}


bool CRDTGraph::in(const int &id) {
    return nodes.in(id);
//    if (nodes.in(id)) {
//        list<N> l = nodes[id].readAsList();
//        if(nodes[id].readAsList().empty())
//            return true;
//        else
//            return false;
//    } else
//        return false;
}

bool CRDTGraph::empty(const int &id) {
    if (nodes.in(id)) {
        list<N> l = nodes[id].readAsList();
        if(nodes[id].readAsList().empty())
            return true;
        else
            return false;
    } else
        return false;
}


void CRDTGraph::insert_or_assign(int id, const N &node) 
{
    try {
        if (nodes[id].getNodes(id).first == node) {
            count++;
            std::cout << "Skip node insertion: " << id << " skipped: " << count << std::endl;
            return;
        }
        count = 0;

        auto delta = nodes[id].add(node, id);
        //writer->update(translateAwCRDTtoICE(id, delta));
        auto val = translateAwCRDTtoICE(id, delta);
        dsrpub.write(&val);
        //TODO: writer
        emit update_node_signal(id, node.type());
    } catch(const std::exception &e){std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}

void CRDTGraph::insert_or_assign(const N &node) {
    try {

        if (nodes[node.id()].getNodes(node.id()).first == node) {
            count++;
            std::cout << "Skip node insertion: " << node.id() << " skipped: " << count << std::endl;
            return;
        }
        count = 0;

        auto delta = nodes[node.id()].add(node, node.id());
        //writer->update(translateAwCRDTtoICE(node.id, delta));
        auto val = translateAwCRDTtoICE(node.id(), delta);
        dsrpub.write(&val);

        //TODO: writer

        emit update_node_signal(node.id(), node.type());
    } catch(const std::exception &e){ std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
}

void CRDTGraph::insert_or_assign(int id, const std::string &type_) {
    //N new_node; new_node.type = type_; new_node.id = id;
    N new_node; 
    new_node.type(type_); 
    new_node.id(id);
    auto delta = nodes[id].add(new_node);
    //writer->update(translateAwCRDTtoICE(id, delta));
    auto val = translateAwCRDTtoICE(id, delta);
    dsrpub.write(&val);

    //TODO: writer

    emit update_node_signal(id, new_node.type());
}


void CRDTGraph::join_delta_node(AworSet aworSet) {
    try{
        auto d = translateAwICEtoCRDT(aworSet);
//        std::lock_guard<std::mutex> lock(_mutex);
        //std::cout << aworSet.id() << std::endl;
        nodes[aworSet.id()].join(d);
        emit update_node_signal(aworSet.id(),d.readAsList().back().type());
    } catch(const std::exception &e){std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};

}

void CRDTGraph::join_full_graph(OrMap full_graph) {
    // Context
    dotcontext<int> dotcontext_aux;
    //auto m = static_cast<std::map<int, int>>(full_graph.cbase().cc());
    std::map<int, int> m;
    for (auto &v : full_graph.cbase().cc())
        m.insert(std::make_pair(v.first(), v.second()));
    std::set <pair<int, int>> s;
    for (auto &v : full_graph.cbase().dc())
        s.insert(std::make_pair(v.first(), v.second()));
    //dotcontext_aux.setContext(m, s);

    /*
     * Esto así no funciona
    for (auto &val : full_graph.m())
    {
        auto awor = translateAwICEtoCRDT(val);
        nodes[val.id()] = awor;
        emit update_node_signal(val.id(), get(val.id()).type());

    }
    */
    //Map
    //TODO: Improve. It is not the most efficient.
    nodes.context().setContext(m, s);

    for (auto &v : full_graph.m())
        for (auto &awv : translateAwICEtoCRDT(v).readAsListWithId()) {
//            std::lock_guard<std::mutex> lock(_mutex);
            nodes[v.id()].add(awv.second, v.id());
            emit update_node_signal(v.id(),get(v.id()).type());
        }

}


void CRDTGraph::print() {
    std::cout << "----------------------------------------\n" << nodes
              << "----------------------------------------" << std::endl;
}


void CRDTGraph::print(int id) {
    std::cout << "----------------------------------------\n" << nodes[id]
              << "\n----------------------------------------" << std::endl;
}


void CRDTGraph::read_from_file(const std::string &file_name)
{
    std::cout << __FUNCTION__ << "Reading xml file: " << file_name << std::endl;

    // Open file and make initial checks
    xmlDocPtr doc;
    if ((doc = xmlParseFile(file_name.c_str())) == NULL)
    {
        fprintf(stderr,"Can't read XML file - probably a syntax error. \n");
        exit(1);
    }
    xmlNodePtr root;
    if ((root = xmlDocGetRootElement(doc)) == NULL)
    {
        fprintf(stderr,"Can't read XML file - empty document\n");
        xmlFreeDoc(doc);
        exit(1);
    }
    if (xmlStrcmp(root->name, (const xmlChar *) "AGMModel"))
    {
        fprintf(stderr,"Can't read XML file - root node != AGMModel");
        xmlFreeDoc(doc);
        exit(1);
    }

    // Read symbols (just symbols, then links in other loop)
    for (xmlNodePtr cur=root->xmlChildrenNode; cur!=NULL; cur=cur->next)
    {
        if (xmlStrcmp(cur->name, (const xmlChar *)"symbol") == 0)
        {
            xmlChar *stype = xmlGetProp(cur, (const xmlChar *)"type");
            xmlChar *sid = xmlGetProp(cur, (const xmlChar *)"id");

            //AGMModelSymbol::SPtr s = newSymbol(atoi((char *)sid), (char *)stype);
            int node_id = std::atoi((char *)sid);
            std::cout << __FILE__ << " " << __FUNCTION__ << ", Node: " << node_id << " " <<  std::string((char *)stype) << std::endl;
            std::string node_type((char *)stype);
            auto rd = QVec::uniformVector(2,-200,200);


            Node n;
            n.type(node_type);
            n.id(node_id);
            insert_or_assign(node_id, n);
            add_node_attrib(node_id, "level", std::int32_t(0));
            add_node_attrib(node_id, "parent", std::int32_t(0));


            // Draw attributes come now
            vector<AttribValue> gatts;
            std::string qname = (char *)stype;
            std::string full_name = std::string((char *)stype) + " [" + std::string((char *)sid) + "]";
            std::tuple<std::string, std::string, int> val = mtype_to_icevalue(full_name);

            AttribValue av;
            av.type(std::get<0>(val));
            av.value(std::get<1>(val));
            av.length(std::get<2>(val));
            av.key("name");
            gatts.push_back(av);

            // color selection
            std::string color = "coral";
            if(qname == "world") color = "SeaGreen";
            else if(qname == "transform") color = "SteelBlue";
            else if(qname == "plane") color = "Khaki";
            else if(qname == "differentialrobot") color = "GoldenRod";
            else if(qname == "laser") color = "GreenYellow";
            else if(qname == "mesh") color = "LightBlue";
            else if(qname == "imu") color = "LightSalmon";

            val = mtype_to_icevalue(color);

            AttribValue av2;
            av2.type(std::get<0>(val));
            av2.value(std::get<1>(val));
            av2.length(std::get<2>(val));
            av2.key("color");
            gatts.push_back(av2);


            add_node_attribs(node_id, gatts);

            xmlFree(sid);
            xmlFree(stype);

            for (xmlNodePtr cur2=cur->xmlChildrenNode; cur2!=NULL; cur2=cur2->next)
            {
                if (xmlStrcmp(cur2->name, (const xmlChar *)"attribute") == 0)
                {
                    xmlChar *attr_key   = xmlGetProp(cur2, (const xmlChar *)"key");
                    xmlChar *attr_value = xmlGetProp(cur2, (const xmlChar *)"value");
                    std::string sk = std::string((char *)attr_key);
                    if( sk == "level" or sk == "parent")
                        add_node_attrib(node_id,sk,std::stoi(std::string((char *)attr_value)));
                    else if( sk == "pos_x" or sk == "pos_y")
                        add_node_attrib(node_id,sk,(float)std::stod(std::string((char *)attr_value)));
                    else
                        add_node_attrib(node_id,sk,std::string((char *)attr_value));

                    xmlFree(attr_key);
                    xmlFree(attr_value);
                }
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"comment") == 0) { }           // coments are always ignored
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
                else { printf("unexpected tag inside symbol: %s\n", cur2->name); exit(-1); } // unexpected tags make the program exit
            }
        }
        else if (xmlStrcmp(cur->name, (const xmlChar *)"link") == 0) { }     // we'll ignore links in this first loop
        else if (xmlStrcmp(cur->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
        else if (xmlStrcmp(cur->name, (const xmlChar *)"comment") == 0) { }  // coments are always ignored
        else { printf("unexpected tag #1: %s\n", cur->name); exit(-1); }      // unexpected tags make the program exit
    }

    // Read links
    for (xmlNodePtr cur=root->xmlChildrenNode; cur!=NULL; cur=cur->next)
    {
        if (xmlStrcmp(cur->name, (const xmlChar *)"link") == 0)
        {
            xmlChar *srcn = xmlGetProp(cur, (const xmlChar *)"src");
            if (srcn == NULL) { printf("Link %s lacks of attribute 'src'.\n", (char *)cur->name); exit(-1); }
            int a = atoi((char *)srcn);
            xmlFree(srcn);

            xmlChar *dstn = xmlGetProp(cur, (const xmlChar *)"dst");
            if (dstn == NULL) { printf("Link %s lacks of attribute 'dst'.\n", (char *)cur->name); exit(-1); }
            int b = atoi((char *)dstn);
            xmlFree(dstn);

            xmlChar *label = xmlGetProp(cur, (const xmlChar *)"label");
            if (label == NULL) { printf("Link %s lacks of attribute 'label'.\n", (char *)cur->name); exit(-1); }
            std::string edgeName((char *)label);
            xmlFree(label);
            vector<AttribValue> attrs;
            for (xmlNodePtr cur2=cur->xmlChildrenNode; cur2!=NULL; cur2=cur2->next)
            {
                if (xmlStrcmp(cur2->name, (const xmlChar *)"linkAttribute") == 0)
                {
                    xmlChar *attr_key   = xmlGetProp(cur2, (const xmlChar *)"key");
                    xmlChar *attr_value = xmlGetProp(cur2, (const xmlChar *)"value");
                    
                    AttribValue av;
                    av.type("string");
                    av.value(std::string((char *)attr_value));
                    av.length(1);
                    av.key(std::string((char *)attr_key));

                    
                    attrs.push_back(av);
                    xmlFree(attr_key);
                    xmlFree(attr_value);
                }
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"comment") == 0) { }           // coments are always ignored
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
                else { printf("unexpected tag inside symbol: %s ==> %s\n", cur2->name,xmlGetProp(cur2, (const xmlChar *)"id") ); exit(-1); } // unexpected tags make the program exit
            }

            std::cout << __FILE__ << " " << __FUNCTION__ << "Edge from " << a << " to " << b << " label "  << edgeName <<  std::endl;
            add_edge(a, b, edgeName);
            add_edge_attrib(a, b, "name", edgeName);

            std::vector<AttribValue> edge_attribs;
            if( edgeName == "RT")   //add level to node b as a.level +1, and add parent to node b as a
            {
                add_node_attrib(b,"level", get_node_level(a)+1);
                add_node_attrib(b,"parent", a);
                RMat::RTMat rt;
                float tx,ty,tz,rx,ry,rz;
                for(auto &v : attrs)
                {
                    if(v.key()=="tx")	tx = std::stof(v.value());
                    if(v.key()=="ty")	ty = std::stof(v.value());
                    if(v.key()=="tz")	tz = std::stof(v.value());
                    if(v.key()=="rx")	rx = std::stof(v.value());
                    if(v.key()=="ry")	ry = std::stof(v.value());
                    if(v.key()=="rz")	rz = std::stof(v.value());
                }
                rt.set(rx, ry, rz, tx, ty, tz);
                //rt.print("in reader");
                this->add_edge_attrib(a, b,"RT",rt);
            }
            else
            {
                this->add_node_attrib(b,"parent",0);
                for(auto &r : attrs)
                    edge_attribs.push_back(r);
            }
            this->add_edge_attribs(a, b, edge_attribs);

            //	  this->addEdgeByIdentifiers(a, b, edgeName, attrs);
        }
        else if (xmlStrcmp(cur->name, (const xmlChar *)"symbol") == 0) { }   // symbols are now ignored
        else if (xmlStrcmp(cur->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
        else if (xmlStrcmp(cur->name, (const xmlChar *)"comment") == 0) { }  // comments are always ignored
        else { printf("unexpected tag #2: %s\n", cur->name); exit(-1); }      // unexpected tags make the program exit
    }
}


void CRDTGraph::replace_node(int id, const N &node) {
    nodes.erase(id);
    insert_or_assign(id, node);
}


void CRDTGraph::start_fullgraph_request_thread() {
    //request_thread = std::thread(&CRDTGraph::fullgraph_request_thread, this);
    fullgraph_request_thread();
}


void CRDTGraph::start_fullgraph_server_thread() {
    //server_thread = std::thread(&CRDTGraph::fullgraph_server_thread, this);
    fullgraph_server_thread();
}


void CRDTGraph::start_subscription_thread(bool showReceived) {
    //read_thread = std::thread(&CRDTGraph::subscription_thread, this, showReceived);
    subscription_thread(showReceived);
}


int CRDTGraph::id() {
    return nodes.getId();
}


DotContext CRDTGraph::context() { // Context to ICE
    DotContext om_dotcontext;
    for (auto &kv_cc : nodes.context().getCcDc().first) {
        PairInt p_i;
        p_i.first(kv_cc.first);
        p_i.second(kv_cc.second);
        om_dotcontext.cc().push_back(p_i);
    }
    for (auto &kv_dc : nodes.context().getCcDc().second){
        PairInt p_i;
        p_i.first(kv_dc.first);
        p_i.second(kv_dc.second);
        om_dotcontext.dc().push_back(p_i);
    }
    return om_dotcontext;
}


vector<AworSet> CRDTGraph::Map() {
    std::lock_guard<std::mutex> lock(_mutex);
    vector<AworSet> m;
    for (auto &kv : nodes.getMap()) { // Map of Aworset to ICE
        aworset<Node, int> n;
        //TODO: Esto.
        auto last = *(--(kv.second.dots().ds.end()));
        n.dots().ds.insert(last);
        n.dots().c = kv.second.dots().c;
        m.push_back(translateAwCRDTtoICE(kv.first, n));
    }
    return m;
}


void CRDTGraph::clear() {
    nodes.reset();
}


void CRDTGraph::subscription_thread(bool showReceived) {



    //DataStorm::Topic <std::string, AworSet> topic(node, "DSR");
    //topic.setReaderDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
    //auto reader = DataStorm::FilteredKeyReader<std::string, AworSet>(topic, DataStorm::Filter<std::string>(
    //        "_regex", filter.c_str())); // Reader excluded self agent
    /*
    if (work)
    {
        std::cout << "Starting reader" << std::endl;
        reader.waitForWriters();
    }
    while (true) {
        if (work) {
            try {
                auto sample = reader.getNextUnread(); // Get sample
                if(showReceived)
                    std::cout << "Received: node " << sample.getValue() << " from " << sample.getKey() << std::endl;
                std::cout << sample.getValue() << std::endl;
                join_delta_node(sample.getValue());
            }
            catch (const std::exception &ex) { cerr << ex.what() << endl; }
        }
    }
    */


	 // RTPS Initialize subscriptor
    auto lambda_general_topic = [&] (eprosima::fastrtps::Subscriber* sub, bool* work, CRDT::CRDTGraph *graph) {
        if (*work) {
            try {
                eprosima::fastrtps::SampleInfo_t m_info;
                AworSet sample;


                std::cout << "Unreaded: " << sub->get_unread_count() << std::endl;
                //read or take
                if (sub->takeNextData(&sample, &m_info)) { // Get sample
                    if(m_info.sampleKind == eprosima::fastrtps::rtps::ALIVE) {
                        if( m_info.sample_identity.writer_guid().is_on_same_process_as(sub->getGuid()) == false) {
                            //std::cout << " Received: node " << sample << " from " << m_info.sample_identity.writer_guid() << std::endl;
                            std::cout << " Received: node from: " << m_info.sample_identity.writer_guid() << std::endl;
                            graph->join_delta_node(sample);
                        } /*else {
                                std::cout << "filtered" << std::endl;
                            }*/
                    }
                }
            }
            catch (const std::exception &ex) { cerr << ex.what() << endl; }
        }

    };
    dsrpub_call = NewMessageFunctor(this, &work, lambda_general_topic);
	auto res = dsrsub.init(dsrparticipant.getParticipant(), "DSR", dsrparticipant.getDSRTopicName(), dsrpub_call);
    std::cout << (res == true ? "Ok" : "Error") << std::endl;

}

// Data Storm based
// void CRDTGraph::fullgraph_server_thread() {
//     std::cout << __FUNCTION__ << "->Entering thread to attend full graph requests" << std::endl;
//     // create topic and filtered reader for new graph requests
//     DataStorm::Topic <std::string, GraphRequest> topic_graph_request(node, "DSR_GRAPH_REQUEST");
//     DataStorm::FilteredKeyReader <std::string, GraphRequest> new_graph_reader(topic_graph_request,
//                                                                                            DataStorm::Filter<std::string>(
//                                                                                                    "_regex",
//                                                                                                    filter.c_str()));
//     topic_graph_request.setWriterDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
//     topic_graph_request.setReaderDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
//     auto processSample = [this](auto sample) {
//         if (work) {
//             work = false;
//             std::cout << sample.getValue().from << " asked for full graph" << std::endl;
//             DataStorm::Topic <std::string, OrMap> topic_answer(node, "DSR_GRAPH_ANSWER");
//             DataStorm::SingleKeyWriter <std::string, OrMap> writer(topic_answer, agent_name,
//                                                                                 agent_name + " Full Graph Answer");

//             topic_answer.setWriterDefaultConfig(
//                     {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
//             topic_answer.setReaderDefaultConfig(
//                     {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
//             writer.add(OrMap{id(), map(), context()});
//             for (auto &[k,v] : map())
//                 std::cout << k << ","<< v<<std::endl;
//             std::cout << "Full graph written from lambda" << std::endl;
//             work = true;
//         }
//     };
//     new_graph_reader.onSamples([processSample](const auto &samples) { for (const auto &s : samples) processSample(s); },
//                                processSample);
//     node.waitForShutdown();
// }

void CRDTGraph::fullgraph_server_thread() 
{
    std::cout << __FUNCTION__ << "->Entering thread to attend full graph requests" << std::endl;
    // Request Topic

    auto lambda_graph_request = [&] (eprosima::fastrtps::Subscriber* sub, bool* work, CRDT::CRDTGraph *graph) {

        eprosima::fastrtps::SampleInfo_t m_info;
        GraphRequest sample;
        //readNextData o takeNextData
        if (sub->takeNextData(&sample, &m_info)) { // Get sample
            if(m_info.sampleKind == eprosima::fastrtps::rtps::ALIVE) {
                if( m_info.sample_identity.writer_guid().is_on_same_process_as(sub->getGuid()) == false) {
                    std::cout << " Received Full Graph request: from " << m_info.sample_identity.writer_guid()
                              << std::endl;
                    //if (*work) {
                    *work = false;
                    OrMap mp;
                    mp.id(graph->id());
                    mp.m(graph->Map());
                    mp.cbase(graph->context());
                    std::cout << "nodos enviados: " << mp.m().size()  << std::endl;

                    auto res_write = dsrpub_request_answer.write(&mp);
                    //std::cout << (res_write == true ? "Ok" : "Error") << std::endl;

                    //writer.add(OrMap{id(), map(), context()});
                    for (auto &k : Map())
                        std::cout << k.id() << "," << k.dk() << std::endl;
                    std::cout << "Full graph written" << std::endl;
                    *work = true;
                    //}
                }
            }
        }

    };
    dsrpub_graph_request_call = NewMessageFunctor(this, &work, lambda_graph_request);

    auto res = dsrsub_graph_request.init(dsrparticipant.getParticipant(), "DSR_GRAPH_REQUEST", dsrparticipant.getRequestTopicName(), dsrpub_graph_request_call);


};





void CRDTGraph::fullgraph_request_thread() {

    bool sync = false;

    // Answer Topic
    auto lambda_request_answer = [&sync] (eprosima::fastrtps::Subscriber* sub, bool* work, CRDT::CRDTGraph *graph) {

        eprosima::fastrtps::SampleInfo_t m_info;
        OrMap sample;
        std::cout << "Mensajes sin leer " << sub->get_unread_count() << std::endl;
        if (sub->takeNextData(&sample, &m_info)) { // Get sample
            if(m_info.sampleKind == eprosima::fastrtps::rtps::ALIVE) {
                if( m_info.sample_identity.writer_guid().is_on_same_process_as(sub->getGuid()) == false) {
                    std::cout << " Received Full Graph from " << m_info.sample_identity.writer_guid() << " whith " << sample.m().size() << " elements" << std::endl;
                    graph->join_full_graph(sample);
                    std::cout << "Synchronized." <<std::endl;
                    sync = true;
                }
            }
        }
    };

    dsrpub_request_answer_call = NewMessageFunctor(this, &work, lambda_request_answer);
    auto res = dsrsub_request_answer.init(dsrparticipant.getParticipant(), "DSR_GRAPH_ANSWER", dsrparticipant.getAnswerTopicName(),dsrpub_request_answer_call);

    //Si no pongo esto no funciona
    sleep(1);

    std::cout << " Requesting the complete graph " << std::endl;
    GraphRequest gr;
    gr.from(agent_name);
    dsrpub_graph_request.write(&gr);

    while (!sync) {
        sleep(5);
    }
    //TODO: Eliminar la suscripción después de terminar?.
    eprosima::fastrtps::Domain::removeSubscriber(dsrsub_request_answer.getSubscriber());
    start_subscription_thread(true);
    /*
    std::cout << __FUNCTION__ << ":" << __LINE__ << "-> Initiating request for full graph requests" << std::endl;
//    std::chrono::time_point <std::chrono::system_clock> start_clock = std::chrono::system_clock::now();

    DataStorm::Topic <std::string, OrMap> topic_answer(node, "DSR_GRAPH_ANSWER");
    DataStorm::FilteredKeyReader <std::string, OrMap> reader(topic_answer,
                                                                          DataStorm::Filter<std::string>("_regex",
                                                                                                         filter.c_str()));
    topic_answer.setWriterDefaultConfig(
            {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
    topic_answer.setReaderDefaultConfig(
            {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});

    DataStorm::Topic <std::string, GraphRequest> topicR(node, "DSR_GRAPH_REQUEST");
    DataStorm::SingleKeyWriter <std::string, GraphRequest> writer(topicR, agent_name,
                                                                               agent_name + " Full Graph Request");

    topicR.setWriterDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
    topicR.setReaderDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
    writer.add(GraphRequest{agent_name});

    std::cout << __FUNCTION__ <<":" << __LINE__ << " Wait for writers:[" << reader.hasWriters() << "]. Data unread:[" << reader.hasUnread() << "]. " << std::endl;
    auto full_graph = reader.getNextUnread();
    join_full_graph(full_graph.getValue());
    std::cout << __FUNCTION__ <<":" << __LINE__ << "Synchronized."<<std::endl;
//    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start_clock).count() <
//        TIMEOUT) {
//        join_full_graph(full_graph.getValue());
//
//        std::cout << __FUNCTION__ << " Finished uploading full graph" << std::endl;
//    }
    */
}

AworSet CRDTGraph::translateAwCRDTtoICE(int id, aworset<N, int> &data) {
    AworSet delta_crdt;
    for (auto &kv_dots : data.dots().ds) {
        PairInt pi;
        pi.first(kv_dots.first.first);
        pi.second(kv_dots.first.second);

        dsValue ds;
        ds.pi(pi);
        ds.n(kv_dots.second);
        delta_crdt.dk().ds().push_back(ds);
    }
    for (auto &kv_cc : data.context().getCcDc().first){
        PairInt pi;
        pi.first(kv_cc.first);
        pi.second(kv_cc.second);
        delta_crdt.dk().cbase().cc().push_back(pi);
    }
    for (auto &kv_dc : data.context().getCcDc().second){
        PairInt pi;
        pi.first(kv_dc.first);
        pi.second(kv_dc.second);

        delta_crdt.dk().cbase().dc().push_back(pi);
    }
    delta_crdt.id(id); //TODO: Check K value of aworset (ID=0)
    return delta_crdt;
}

aworset<N, int> CRDTGraph::translateAwICEtoCRDT(AworSet &data) {
    // Context
    dotcontext<int> dotcontext_aux;
    //auto m = static_cast<std::map<int, int>>(data.dk().cbase().cc());
    std::map<int, int> m;
    for (auto &v : data.dk().cbase().cc())
        m.insert(std::make_pair(v.first(), v.second()));
    std::set <pair<int, int>> s;
    for (auto &v : data.dk().cbase().dc())
        s.insert(std::make_pair(v.first(), v.second()));
    dotcontext_aux.setContext(m, s);
    // Dots
    std::map <pair<int, int>, N> ds_aux;
    for (auto &v : data.dk().ds())
        ds_aux[pair<int, int>(v.pi().first(), v.pi().second())] = v.n();
    // Join
    aworset<N, int> aw = aworset<N, int>(data.id());
    aw.setContext(dotcontext_aux);
    aw.dots().set(ds_aux);
    return aw;
}


// Converts Ice string values into Mtypes
CRDT::MTypes CRDTGraph::icevalue_to_mtypes(const std::string &type, const std::string &val)
{
    // WE NEED TO ADD A TYPE field to the Attribute values and get rid of this shit
    static const std::list<std::string> string_types{ "imName", "imType", "tableType", "texture", "engine", "path", "render", "color", "type"};
    static const std::list<std::string> bool_types{ "collidable", "collide"};
    static const std::list<std::string> RT_types{ "RT"};
    static const std::list<std::string> vector_float_types{ "laser_data_dists", "laser_data_angles"};

    DSR::MTypes res;
    if(std::find(string_types.begin(), string_types.end(), type) != string_types.end())
        res = val;
    else if(std::find(bool_types.begin(), bool_types.end(), type) != bool_types.end())
    { if( val == "true") res = true; else res = false; }
    else if(std::find(vector_float_types.begin(), vector_float_types.end(), type) != vector_float_types.end())
    {
        std::vector<float> numbers;
        std::istringstream iss(val);
        std::transform(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
                       std::back_inserter(numbers), [](const std::string &s){ return (float)std::stof(s);});
        res = numbers;

    }
        // instantiate a QMat from string marshalling
    else if(std::find(RT_types.begin(), RT_types.end(), type) != RT_types.end())
    {
        std::vector<float> ns;
        std::istringstream iss(val);
        std::transform(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
                       std::back_inserter(ns), [](const std::string &s){ return QString::fromStdString(s).toFloat();});
        RMat::RTMat rt;
        // std::cout << "------ in translating ------";
        // for(auto n:ns) std::cout << n << " ";
        // std::cout << std::endl;
        if(ns.size() == 6)
        {
            rt.set(ns[3],ns[4],ns[5],ns[0],ns[1],ns[2]);
            //rt.print("in translate");
        }
        else
        {
            std::cout << __FUNCTION__ <<":"<<__LINE__<< "Error reading RTMat. Initializing with identity";
            rt = QMat::identity(4);
        }
        return(rt);
    }
    else
    {
        try
        { res = (float)std::stod(val); }
        catch(const std::exception &e)
        { std::cout << __FUNCTION__ <<":"<<__LINE__<<" catch: " << type << " " << val << std::endl; res = std::string{""}; }
    }
    return res;
}

std::tuple<std::string, std::string, int> CRDTGraph::mtype_to_icevalue(const MTypes &t)
{
    return std::visit(overload
      {
              [](RMat::RTMat m) -> std::tuple<std::string, std::string, int> { return make_tuple("RTMat", m.serializeAsString(),m.getDataSize()); },
              [](std::vector<float> a)-> std::tuple<std::string, std::string, int>
              {
                  std::string str;
                  for(auto &f : a)
                      str += std::to_string(f) + " ";
                  return make_tuple("vector<float>",  str += "\n",a.size());
              },
              [](std::string a) -> std::tuple<std::string, std::string, int>								{ return  make_tuple("string", a,1); },
              [](auto a) -> std::tuple<std::string, std::string, int>										{ return make_tuple(typeid(a).name(), std::to_string(a),1);}
      }, t);
}

std::string CRDTGraph::printVisitor(const MTypes &t)
{
    return std::visit(overload
      {
              [](RMat::RTMat m) -> std::string								{ return m.serializeAsString(); },
              [](std::vector<float> a)-> std::string
              {
                  std::string str;
                  for(auto &f : a)
                      str += std::to_string(f) + " ";
                  return  str += "\n";
              },
              [](std::string a) -> std::string								{ return  a; },
              [](auto a) -> std::string										{ return std::to_string(a);}
      }, t);
}