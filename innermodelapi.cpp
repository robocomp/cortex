/*
 * Copyright 2018 <copyright holder> <email>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use graph file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "innermodelapi.h"

using namespace DSR;

void InnerModelAPI::innerModelTreeWalk(const IMType &id)
{
	auto r = graph->getNodeByInnerModelName("imName", id.toStdString());
	std::cout << "ID " << r << std::endl;
	innerModelTreeWalk(r);
}
void InnerModelAPI::innerModelTreeWalk(const IDType &id)
{
	//std::cout << "id: " << id << std::endl;

	if (graph->nodeExists(id) == false)
	{
		std::cout << __FUNCTION__ << "Non existing node: " << id << std::endl;
		return;
	}
	std::cout << "node: " << id << std::endl;
	for(auto &child_id : graph->edgesByLabel(id, "RT")) 
	{
		innerModelTreeWalk(child_id);
		//exit(-1);
	} 
}

RMat::QVec InnerModelAPI::transform(const IMType &destId, const QVec &initVec, const IMType &origId)
{
    if (initVec.size()==3)
	{
		return (getTransformationMatrix(destId, origId) * initVec.toHomogeneousCoordinates()).fromHomogeneousCoordinates();
	}
	else if (initVec.size()==6)
	{
		const QMat M = getTransformationMatrix(destId, origId);
		const QVec a = (M * initVec.subVector(0,2).toHomogeneousCoordinates()).fromHomogeneousCoordinates();
		const Rot3D R(initVec(3), initVec(4), initVec(5));
		const QVec b = (M.getSubmatrix(0,2,0,2)*R).extractAnglesR_min();
		QVec ret(6);
		ret(0) = a(0);
		ret(1) = a(1);
		ret(2) = a(2);
		ret(3) = b(0);
		ret(4) = b(1);
		ret(5) = b(2);
		return ret;
	}
	// else
	// {
	// 	throw InnerModelException("InnerModel::transform was called with an unsupported vector size.");
	// }
}

RMat::RTMat InnerModelAPI::getTransformationMatrix(const IDType &to, const IDType &from)
{
    
	RMat::RTMat ret;

	// if (localHashTr.contains(QPair<QString, QString>(to, from)))
	// {
	// 	ret = localHashTr[QPair<QString, QString>(to, from)];
	// }
	// else
	// {
		auto [listA, listB] = setLists(from, to);
		for (auto to = listA.begin(); to != std::prev(listA.end()); ++to)
		{
			//ret = ((RTMat)(*i)).operator*(ret);
			std::cout << "List A id " << *to << std::endl;
			ret = graph->edgeAttrib<RMat::RTMat>(*(std::next(to,1)), *to, "RT") * ret;
		}
		for (auto from = listB.begin(); from != std::prev(listB.end()); ++from)
		{
			//ret = i->invert() * ret;
			ret = graph->edgeAttrib<RMat::RTMat>(*from, *(std::next(from)), "RT").invert() * ret;
			std::cout << "List B id " << *from << std::endl;
		}
		//localHashTr[QPair<QString, QString>(to, from)] = ret;
	// }
	return ret;
} 
 
RMat::RTMat InnerModelAPI::getTransformationMatrix(const IMType &to_, const IMType &from_)
{
    auto to = graph->getNodeByInnerModelName("imName", to_.toStdString());
    auto from = graph->getNodeByInnerModelName("imName", from_.toStdString());
	
	return getTransformationMatrix(to, from);	
} 

 RTMat  InnerModelAPI::getTransformationMatrixS(const std::string &to_, const std::string &from_)
 {
	auto to = graph->getNodeByInnerModelName("imName", to_);
    auto from = graph->getNodeByInnerModelName("imName", from_);
	
	getTransformationMatrix(to, from);
 }

QMat InnerModelAPI::getRotationMatrixTo(const IMType &to_, const IMType &from_)
{
	QMat rret = QMat::identity(3);

	// if (localHashRot.contains(QPair<QString, QString>(to, from)))
	// {
	// 	rret = localHashRot[QPair<QString, QString>(to, from)];
	// }
	//else
	//{
		auto to = graph->getNodeByInnerModelName("imName", to_.toStdString());
    	auto from = graph->getNodeByInnerModelName("imName", from_.toStdString());
		
		//InnerModelTransform *tf=NULL;
		auto [listA, listB] = setLists(from, to);
		for (auto to = listA.begin(); to != std::prev(listA.end()); ++to)
		{
			//ret = ((RTMat)(*i)).operator*(ret);
			std::cout << "getRotationMatrix List A id " << *to << std::endl;
			rret = graph->edgeAttrib<RMat::RTMat>(*(std::next(to,1)), *to, "RT").getR() * rret;
		}
		for (auto from = listB.begin(); from != std::prev(listB.end()); ++from)
		{
			//ret = i->invert() * ret;
			std::cout << "getRotationMatrix List B id " << *from << std::endl;
			rret = graph->edgeAttrib<RMat::RTMat>(*from, *(std::next(from)), "RT").getR().transpose() * rret;
		}
		//localHashRot[QPair<QString, QString>(to, from)] = rret;
	//}
	return rret;
}

QVec InnerModelAPI::getTranslationVectorTo(const IMType &to_, const IMType &from_)
{
	QMat m = this->getTransformationMatrix(to_, from_);
	return m.getCol(3);
}

QVec InnerModelAPI::rotationAngles(const IMType & destId_, const IMType & origId_)
{
	return getTransformationMatrix(destId_, origId_).extractAnglesR();
}
       	

InnerModelAPI::ABLists InnerModelAPI::setLists(const IDType &origId, const IDType &destId)
{
	//InnerModelNode *a = hash[origId], *b = hash[destId];
    std::list<IDType> listA, listB;
	IDType a = origId;
	IDType b = destId;
	
/* 	if (!a)
		throw InnerModelException("Cannot find node: \""+ origId.toStdString()+"\"");
	if (!b)
		throw InnerModelException("Cannot find node: "+ destId.toStdString()+"\"");
 */
	std::int32_t a_level = graph->getNodeLevel(origId);
	std::int32_t b_level = graph->getNodeLevel(destId);
	std::int32_t min_level = std::min(a_level,b_level);
	 
	//std::cout << "caca " << a_level << " " << b_level << " " << min_level << std::endl;
	listA.clear();
	while (a_level >= min_level)
	{
		listA.push_back(a);
		if(graph->getParent(a) == 0)
			break;
		a = graph->getParent(a);
	}
	//std::cout << "caca2 "  << std::endl;
	listB.clear();
	while (b_level >= min_level)
	{
		listB.push_front(b);
		if(graph->getParent(b) == 0)
			break;
		b = graph->getParent(b);
	}
	//std::cout << "caca3 " << std::endl;
	while (b != a)
	{
		listA.push_back(a);
		listB.push_front(b);
		a = graph->getParent(a);
		b = graph->getParent(b);
	}

	for(auto a : listA)
		std::cout << "list A " << a << std::endl;
	for(auto a : listB)
		std::cout << "list B " << a << std::endl;
	return std::make_pair(listA, listB);
}

/////////////////////////////////////////////////////
///// Tree update methods
/////////////////////////////////////////////////////

void InnerModelAPI::updateTransformValues(const IMType &transformId_, float tx, float ty, float tz, float rx, float ry, float rz, const IMType &parentId_)
{	
//	cleanupTables();
//	InnerModelTransform *aux = dynamic_cast<InnerModelTransform *>(hash[transformId]);
    auto transformId = graph->getNodeByInnerModelName("imName", transformId_.toStdString());
    auto parentId = graph->getNodeByInnerModelName("imName", parentId_.toStdString());

	if(graph->nodeExists(transformId) and graph->nodeHasAttrib<std::string>(transformId, "imType", "transform"))
	{
		if(parentId != 0)
		{
			//InnerModelTransform *auxParent = dynamic_cast<InnerModelTransform *>(hash[parentId]);
			if (graph->nodeExists(parentId) and graph->nodeHasAttrib<std::string>(parentId, "imType", "transform"))
			{
				RTMat Tbi;
				Tbi.setTr(tx,ty,tz);
				Tbi.setR (rx,ry,rz);
				//RTMat Tpb = getTransformationMatrix( getNode (transformId)->parent->id,parentId );
                RTMat Tpb = getTransformationMatrix( graph->getParent(transformId), parentId );
				RTMat Tpi = Tpb*Tbi;
				QVec angles = Tpi.extractAnglesR();
				QVec tr = Tpi.getTr();
				rx = angles.x();
				ry = angles.y();
				rz = angles.z();
				tx = tr.x();
				ty = tr.y();
				tz = tr.z();
			}
			else
			{
				qDebug() << "There is no such" << parentId << "node";
			}
		}
		//always update
		//aux->update(tx,ty,tz,rx,ry,rz);
        Attribs attr{ std::pair("tx",tx), std::pair("ty",ty), std::pair("tz",tz), std::pair("rx",rx), std::pair("ry",ry), std::pair("rz",rz) };
        graph->addNodeAttribs(transformId, attr);
	}
	else
	{
		qDebug() << "There is no such" << transformId << "node";
	}
}