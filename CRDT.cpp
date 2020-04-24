//
// Created by crivac on 5/02/19.
//


#include "CRDT.h"
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <QXmlSimpleReader>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>

#include <fastrtps/subscriber/Subscriber.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include <fastrtps/Domain.h>

using namespace CRDT;


// PUBLIC METHODS

CRDTGraph::CRDTGraph(int root, std::string name, int id) : agent_id(id) {
    graph_root = root;
    agent_name = name;
    nodes = Nodes(graph_root);

    work = true;

    // RTPS Create participant 
	auto [suc , participant_handle] = dsrparticipant.init();

    // General Topic
	// RTPS Initialize publisher
	dsrpub.init(participant_handle, "DSR", dsrparticipant.getDSRTopicName());
    dsrpub_graph_request.init(participant_handle, "DSR_GRAPH_REQUEST", dsrparticipant.getRequestTopicName());
    dsrpub_request_answer.init(participant_handle, "DSR_GRAPH_ANSWER", dsrparticipant.getAnswerTopicName());

}


CRDTGraph::~CRDTGraph() {
}

/*
 * NODE METHODS
 */

Node CRDTGraph::get_node(const std::string& name) {
    std::shared_lock<std::shared_mutex>  lock(_mutex);
    try {
        if (name.empty()) {
            Node n;
            n.type("error");
            n.agent_id(agent_id);
            n.id(-1);
            return n;
        };
        int id = get_id_from_name(name);
        if (id != -1) {
            auto n = &nodes[id].dots().ds.rbegin()->second;
            if (n->name() == name) return Node(*n);
        }

    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;
    };
    Node n;
    n.type("error");
    n.agent_id(agent_id);
    n.id(-1);
    return n;
}


Node CRDTGraph::get_node(int id) {
    std::shared_lock<std::shared_mutex>  lock(_mutex);
    return get_(id);
}

bool CRDTGraph::insert_or_assign_node(const N &node) {
    bool r;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        r = insert_or_assign_node_(node);
    }
    emit update_node_signal(node.id(), node.type());
    return r;
}

bool CRDTGraph::insert_or_assign_node_(const N &node) {
    try {
        if (nodes[node.id()].getNodesSimple(node.id()).first == node) {
            count++;
            //std::cout << "Skip node insertion: " << node.id() << " skipped: " << count << std::endl;
            return true;
        }
        count = 0;
        aworset<Node, int> delta = nodes[node.id()].add(node, node.id());
        name_map[node.name()] = node.id();
        id_map[node.id()] = node.name();


        auto val = translateAwCRDTtoICE(node.id(), delta);
        dsrpub.write(&val);

        return true;
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;
    };
    return false;
}


bool CRDTGraph::delete_node(const std::string& name) {
    vector<tuple<int,int, std::string>> edges;
    int id = -1;
    std::unique_lock<std::shared_mutex>  lock(_mutex);
    id = get_id_from_name(name);
    if(id == -1) 
        return false;
    return delete_node(id);
}

bool CRDTGraph::delete_node(int id) 
{
    // std::cout << "Tomando lock único para borrar un nodo" << std::endl;
    vector<tuple<int,int, std::string>> edges;
    std::unique_lock<std::shared_mutex>  lock(_mutex);
    try 
    {
        //1. Get and remove node.
        auto node = get_(id);
        for (auto v : node.fano()) { // Delete all edges from this node.
            std::cout << id << " -> " << v.first << std::endl;
            edges.emplace_back(make_tuple(id, v.first, v.second.label()));
        }
        nodes.erase(id);
        name_map.erase(name);
        id_map.erase(id);
        //2. search and remove edges.
        //For each node check if there is an edge to remove.
        for (auto [k, v] : nodes.getMapRef()) {
            //get the actual value of a node.
            auto visited_node =  Node(v.dots().ds.rbegin()->second);
            auto value = visited_node.fano().find(id);
            //if nodes are not connected continue.
            if (value == visited_node.fano().end())  continue;

            //Necesitamos una copia?
            visited_node.fano().erase(value);
            auto delta = nodes[visited_node.id()].add(visited_node, visited_node.id());
            edges.emplace_back(make_tuple(visited_node.id(), id, value->second.label()));

            // Send changes.
            auto val = translateAwCRDTtoICE(visited_node.id(), delta);
            dsrpub.write(&val);
        }
        return true;
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;
    };

    emit del_node_signal(id);

    for (auto &[id0, id1, label] : edges) {
        emit del_edge_signal(id0, id1, label);
    }

    return false;
}



/*
 * EDGE METHODS
 */
EdgeAttribs CRDTGraph::get_edge(const std::string& from, const std::string& to) {
    std::shared_lock<std::shared_mutex>  lock(_mutex);

    int id_from = get_id_from_name(from);
    int id_to = get_id_from_name(to);

    try { if(in(id_from) && in(id_to)) {
            auto n = get(id_from);
            auto edge = n.fano().find(id_to);
            if (edge != n.fano().end()) {
                return EdgeAttribs(edge->second);
            }
            else {
                std::cout <<"Error obteniedo edge from: "<< from  << " to: " << to << endl;

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

bool CRDTGraph::insert_or_assign_edge(const EdgeAttribs& attrs) {

    std::unique_lock<std::shared_mutex>  lock(_mutex);
    int from = attrs.from();
    int to = attrs.to();

    try {
        if (in(from)  && in(to)) {
            auto node = get_(from);
            auto edge = node.fano().find(to);
            if (edge != node.fano().end()) {
                edge->second = attrs;
            } else {
                node.fano().insert(make_pair(to, attrs));
            }

            node.agent_id(agent_id);
            insert_or_assign_node_(node);

        }
            else {
                std::cout << __FUNCTION__ <<":" << __LINE__ <<" Error. ID:"<<from<<" or "<<to<<" not found. Cant update. "<< std::endl;
            return false;
        }
    }
    catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;
        return false;
    }
    emit update_edge_signal( attrs.from(),  attrs.to());

    return true;
}


bool CRDTGraph::delete_edge(const std::string& from, const std::string& to) {
    int id_from = 0;
    int id_to = 0;
    try {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        id_from = get_id_from_name(from);
        id_to = get_id_from_name(to);

        if (in(id_from) && in(id_to)) {
            auto node = get_(id_from);
            auto edge = node.fano().find(id_to);
            if (edge == node.fano().end()) { return false; }
            node.fano().erase(edge);

            node.agent_id(agent_id);
            insert_or_assign_node_(node);

        } else { return false; }
    } catch (const std::exception &e) {
            std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                      << std::endl;
            return false;
        };

    emit update_edge_signal(id_from, id_to);

    return true;

}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

Nodes CRDTGraph::get() {
    std::shared_lock<std::shared_mutex>  lock(_mutex);
    return nodes;
}


N CRDTGraph::get(int id) {
    std::shared_lock<std::shared_mutex>  lock(_mutex);
    return get_(id);
}

N CRDTGraph::get_(int id) {
    try 
    {
        if (in(id)) {
            if (!nodes[id].dots().ds.empty()) {
                return nodes[id].dots().ds.rbegin()->second;
            }
            else {
                Node n;
                n.type("empty");
                n.id(-1);
                return n;
            };
        }
    } catch(const std::exception &e)
    {
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << "-> "<<id<<std::endl;
        Node n;
        n.type("error");
        n.id(-1);
        return n;
    }
    Node n;
    n.type("error");
    n.id(-1);
    return n;
}


AttribValue CRDTGraph::get_node_attrib_by_name(const Node& n, const std::string &key) {
    try {
        auto attrs = n.attrs();
        auto value  = attrs.find(key);

        if (value != attrs.end()) {
            return value->second;
        }

        AttribValue av;
        av.type("unknown");
        av.value("unknow");
        av.key(key);
        av.length(0);

        return av;
    }
    catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << "-> "<<n.id()<<std::endl;
        
        AttribValue av;
        av.type("unknown");
        av.value("unknow");
        av.length(0);
        
        return av;
    };
}



std::int32_t CRDTGraph::get_node_level(Node& n){
    try {
        return std::stoi(get_node_attrib_by_name(n, "level").value());
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl; };

    return -1;
}


std::string CRDTGraph::get_node_type(Node& n) {
    try {
        return n.type();
    } catch(const std::exception &e){
        std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};
    return "error";
}



int CRDTGraph::get_id_from_name(const std::string &name) {
        auto v = name_map.find(name);
        if (v != name_map.end()) return v->second;
        return   -1;

}


std::string CRDTGraph::get_name_from_id(std::int32_t id) {
    auto v = id_map.find(id);
    if (v != id_map.end()) return v->second;
    return   "error";
}


bool CRDTGraph::in(const int &id) {
    return nodes.in(id);
}

bool CRDTGraph::empty(const int &id) {
    if (nodes.in(id)) {
        return nodes[id].dots().ds.empty();
    } else
        return false;
}


void CRDTGraph::join_delta_node(AworSet aworSet) {
    try{
        auto d = translateAwICEtoCRDT(aworSet);
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            nodes[aworSet.id()].join_replace(d);
            name_map[nodes[aworSet.id()].dots().ds.rbegin()->second.name()] = aworSet.id();
            id_map[aworSet.id()] = nodes[aworSet.id()].dots().ds.rbegin()->second.name();
        }
        emit update_node_signal(aworSet.id(), d.dots().ds.rbegin()->second.type());
    } catch(const std::exception &e){std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl;};

}

void CRDTGraph::join_full_graph(OrMap full_graph) {
    vector<pair<int, std::string>> updates;
            m.emplace(std::make_pair(v.first, v.second));
        std::set<pair<int, int>> s;
        for (auto &v : full_graph.cbase().dc())
            s.emplace(std::make_pair(v.first(), v.second()));
        //dotcontext_aux.setContext(m, s);
        nodes.context().setContext(m, s);


        for (auto &[k,val] : full_graph.m()) {
            auto awor = translateAwICEtoCRDT(val);
            {
                nodes[k].add(awor.dots().ds.begin()->second, awor.dots().ds.begin()->second.id());
                name_map[nodes[k].dots().ds.rbegin()->second.name()] = k;
                id_map[k] = nodes[k].dots().ds.rbegin()->second.name();
                updates.emplace_back(make_pair(k, awor.dots().ds.begin()->second.type()));
            }

        }
    }

    for (auto &[id, type] : updates)
        emit update_node_signal(id, type);

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
            xmlChar *sname = xmlGetProp(cur, (const xmlChar *)"name");

            //AGMModelSymbol::SPtr s = newSymbol(atoi((char *)sid), (char *)stype);
            int node_id = std::atoi((char *)sid);
            std::cout << __FILE__ << " " << __FUNCTION__ << ", Node: " << node_id << " " <<  std::string((char *)stype) << std::endl;
            std::string node_type((char *)stype);
            auto rd = QVec::uniformVector(2,-200,200);
            std::string name((char *)sname);

            Node n;
            n.type(node_type);
            n.id(node_id);
            n.agent_id(agent_id);
            n.name(name);

            name_map[name] = node_id;
            id_map[node_id] = name;

            std::map<string, AttribValue> attrs;

            add_attrib(attrs, "level",std::int32_t(0));
            add_attrib(attrs, "parent",std::int32_t(0));


            std::string qname = (char *)stype;
            std::string full_name = std::string((char *)stype) + " [" + std::string((char *)sid) + "]";

            add_attrib(attrs, "name",full_name);


            // color selection
            std::string color = "coral";
            if(qname == "world") color = "SeaGreen";
            else if(qname == "transform") color = "SteelBlue";
            else if(qname == "plane") color = "Khaki";
            else if(qname == "differentialrobot") color = "GoldenRod";
            else if(qname == "laser") color = "GreenYellow";
            else if(qname == "mesh") color = "LightBlue";
            else if(qname == "imu") color = "LightSalmon";

            add_attrib(attrs, "color", color);


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
                        add_attrib(attrs, sk, std::stoi(std::string((char *)attr_value)));
                    else if( sk == "pos_x" or sk == "pos_y")
                        add_attrib(attrs, sk, (float)std::stod(std::string((char *)attr_value)));
                    else
                        add_attrib(attrs, sk, std::string((char *)attr_value));


                    xmlFree(attr_key);
                    xmlFree(attr_value);
                }
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"comment") == 0) { }           // coments are always ignored
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
                else { printf("unexpected tag inside symbol: %s\n", cur2->name); exit(-1); } // unexpected tags make the program exit
            }

            n.attrs(attrs);
            insert_or_assign_node(n);
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
            std::map<string, AttribValue> attrs;
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

                    
                    attrs[std::string((char *)attr_key)] = av;
                    xmlFree(attr_key);
                    xmlFree(attr_value);
                }
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"comment") == 0) { }           // coments are always ignored
                else if (xmlStrcmp(cur2->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
                else { printf("unexpected tag inside symbol: %s ==> %s\n", cur2->name,xmlGetProp(cur2, (const xmlChar *)"id") ); exit(-1); } // unexpected tags make the program exit
            }

            std::cout << __FILE__ << " " << __FUNCTION__ << "Edge from " << a << " to " << b << " label "  << edgeName <<  std::endl;

            EdgeAttribs ea;
            ea.from(a);
            ea.to(b);
            ea.label(edgeName);
            std::map<string, AttribValue> attrs_edge;

            auto val = mtype_to_icevalue(edgeName);
            AttribValue av = AttribValue();

            av.type(std::get<0>(val));
            av.value( std::get<1>(val));
            av.length(std::get<2>(val));
            av.key("name");

            attrs["name"] = av;


            if( edgeName == "RT")   //add level to node b as a.level +1, and add parent to node b as a
            {

                Node n = get(b);


                add_attrib(n.attrs(),"level", get_node_level(n)+1);
                add_attrib(n.attrs(),"parent", a);
                RMat::RTMat rt;
                float tx,ty,tz,rx,ry,rz;
                for(auto &[key, v] : attrs)
                {
                    if(key=="tx")	tx = std::stof(v.value());
                    if(key=="ty")	ty = std::stof(v.value());
                    if(key=="tz")	tz = std::stof(v.value());
                    if(key=="rx")	rx = std::stof(v.value());
                    if(key=="ry")	ry = std::stof(v.value());
                    if(key=="rz")	rz = std::stof(v.value());
                }
                rt.set(rx, ry, rz, tx, ty, tz);
                //rt.print("in reader");
                add_attrib(attrs_edge, "RT", rt);
                //this->add_edge_attrib(a, b,"RT",rt);

                insert_or_assign_node(n);
            }
            else
            {
                Node n = get(b);
                add_attrib(n.attrs(),"parent",0);
                insert_or_assign_node(n);
                for(auto &[k,v] : attrs)
                    attrs_edge[k] = v;

            }


            ea.attrs(attrs_edge);
            insert_or_assign_edge(ea);

        }
        else if (xmlStrcmp(cur->name, (const xmlChar *)"symbol") == 0) { }   // symbols are now ignored
        else if (xmlStrcmp(cur->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
        else if (xmlStrcmp(cur->name, (const xmlChar *)"comment") == 0) { }  // comments are always ignored
        else { printf("unexpected tag #2: %s\n", cur->name); exit(-1); }      // unexpected tags make the program exit
    }
}



void CRDTGraph::start_fullgraph_request_thread() {
    fullgraph_request_thread();
}


void CRDTGraph::start_fullgraph_server_thread() {
    fullgraph_server_thread();
}


void CRDTGraph::start_subscription_thread(bool showReceived) {
    subscription_thread(showReceived);
}


int CRDTGraph::id() {
    return nodes.getId();
}


DotContext CRDTGraph::context() { // Context to ICE
    DotContext om_dotcontext;
    for (auto &kv_cc : nodes.context().getCcDc().first) {
        om_dotcontext.cc().emplace(make_pair(kv_cc.first, kv_cc.second));
    }
    for (auto &kv_dc : nodes.context().getCcDc().second){
        PairInt p_i;
        p_i.first(kv_dc.first);
        p_i.second(kv_dc.second);
        om_dotcontext.dc().push_back(p_i);
    }
    return om_dotcontext;
}


std::map<int,AworSet> CRDTGraph::Map() {
    std::shared_lock<std::shared_mutex>  lock(_mutex);
    std::map<int,AworSet>  m;
    for (auto kv : nodes.getMapRef()) { // Map of Aworset to ICE
        aworset<Node, int> n;

        auto last = *kv.second.dots().ds.rbegin();
        n.dots().ds.insert(last);
        n.dots().c = kv.second.dots().c;
        m[kv.first] = translateAwCRDTtoICE(kv.first, n);
    }
    return m;
}


void CRDTGraph::subscription_thread(bool showReceived) {

	 // RTPS Initialize subscriptor
    auto lambda_general_topic = [&] (eprosima::fastrtps::Subscriber* sub, bool* work, CRDT::CRDTGraph *graph) {
        if (*work) {
            try {
                eprosima::fastrtps::SampleInfo_t m_info;
                AworSet sample;


                //std::cout << "Unreaded: " << sub->get_unread_count() << std::endl;
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

                    *work = false;
                    OrMap mp;
                    mp.id(graph->id());
                    mp.m(graph->Map());
                    mp.cbase(graph->context());
                    std::cout << "nodos enviados: " << mp.m().size()  << std::endl;

                    dsrpub_request_answer.write(&mp);

                    for (auto &[k, v] : Map())
                        std::cout << k << "," << v.dk() << std::endl;
                    std::cout << "Full graph written" << std::endl;
                    *work = true;

                }
            }
        }

    };
    dsrpub_graph_request_call = NewMessageFunctor(this, &work, lambda_graph_request);

    dsrsub_graph_request.init(dsrparticipant.getParticipant(), "DSR_GRAPH_REQUEST", dsrparticipant.getRequestTopicName(), dsrpub_graph_request_call);


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
    dsrsub_request_answer.init(dsrparticipant.getParticipant(), "DSR_GRAPH_ANSWER", dsrparticipant.getAnswerTopicName(),dsrpub_request_answer_call);

    sleep(1);

    std::cout << " Requesting the complete graph " << std::endl;
    GraphRequest gr;
    gr.from(agent_name);
    dsrpub_graph_request.write(&gr);

    while (!sync) {
        sleep(1);
    }
    eprosima::fastrtps::Domain::removeSubscriber(dsrsub_request_answer.getSubscriber());
    start_subscription_thread(true);

}

AworSet CRDTGraph::translateAwCRDTtoICE(int id, aworset<N, int> &data) {
    AworSet delta_crdt;
    for (auto &kv_dots : data.dots().ds) {
        PairInt pi;
        pi.first(kv_dots.first.first);
        pi.second(kv_dots.first.second);

        delta_crdt.dk().ds().emplace(make_pair(pi, kv_dots.second));
    }
    for (auto &kv_cc : data.context().getCcDc().first){
        delta_crdt.dk().cbase().cc().emplace(make_pair(kv_cc.first, kv_cc.second));
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
        m.insert(std::make_pair(v.first, v.second));
    std::set <pair<int, int>> s;
    for (auto &v : data.dk().cbase().dc())
        s.insert(std::make_pair(v.first(), v.second()));
    dotcontext_aux.setContext(m, s);
    // Dots
    std::map <pair<int, int>, N> ds_aux;
    for (auto &[k,v] : data.dk().ds())
        ds_aux[pair<int, int>(k.first(), k.second())] = v;
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

    MTypes res;
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
        if(ns.size() == 6)
        {
            rt.set(ns[3],ns[4],ns[5],ns[0],ns[1],ns[2]);
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


void CRDTGraph::add_attrib(std::map<string, AttribValue> &v, std::string att_name, CRDT::MTypes att_value) {
    auto val = mtype_to_icevalue(att_value);

    AttribValue av;
    av.type(std::get<0>(val));
    av.value( std::get<1>(val));
    av.length(std::get<2>(val));
    av.key(att_name);

    v[att_name] = av;
}



