// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*!
 * @file particpant.h
 * This header file contains the declaration of the participant functions.
 */


#ifndef _PARTICIPANT_H_
#define _PARTICIPANT_H_

#include <fastrtps/fastrtps_fwd.h>
#include "../topics/DSRGraphPubSubTypes.h"
#include "dsrpublisher.h"
#include "dsrsubscriber.h"

class DSRParticipant
{
public:
	DSRParticipant();
	virtual ~DSRParticipant();
	std::tuple<bool, eprosima::fastrtps::Participant *> init(int32_t agent_id);
	//void run();
	eprosima::fastrtps::rtps::GUID_t getID() const ;
	const char* getNodeTopicName() const;
	const char* getRequestTopicName() const;
	const char* getAnswerTopicName() const;
    const char* getEdgeTopicName() const;
    const char* getNodeAttrTopicName() const;
    const char* getEdgeAttrTopicName() const;

	eprosima::fastrtps::Participant* getParticipant();

private:
	eprosima::fastrtps::Participant *mp_participant{};
    eprosima::fastrtps::Publisher *mp_node_p; //"DSR_NODE"
	eprosima::fastrtps::Subscriber *mp_node_s; //"DSR_NODE"
 	MvregPubSubType dsrgraphType;

	eprosima::fastrtps::Subscriber *mp_subscriber_graph_request; // "DSR_GRAPH_REQUEST"
	GraphRequestPubSubType graphrequestType;
    
	eprosima::fastrtps::Publisher *mp_publisher_topic_answer; //"DSR_GRAPH_ANSWER"
	eprosima::fastrtps::Subscriber *mp_subscriber_topic_answer; ///"DSR_GRAPH_ANSWER"
	OrMapPubSubType graphRequestAnswerType;


    eprosima::fastrtps::Publisher *mp_edge_p; //"DSR_EDGE"
    eprosima::fastrtps::Subscriber *mp_edge_s; ///"DSR_EDGE"
    MvregEdgePubSubType dsrEdgeType;

    eprosima::fastrtps::Publisher *mp_attr_node_p; //"DSR_NODE_ATTRS"
    eprosima::fastrtps::Subscriber *mp_attr_node_s; ///"DSR_NODE_ATTRS"
    MvregNodeAttrPubSubType dsrNodeAttrType;


    eprosima::fastrtps::Publisher *mp_attr_edge_p; //"DSR_EDGE_ATTRS"
    eprosima::fastrtps::Subscriber *mp_attr_edge_s; ///"DSR_EDGE_ATTRS"
	MvregEdgeAttrPubSubType dsrEdgeAttrType;

};

#endif // _Participant_H_