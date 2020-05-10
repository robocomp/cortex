
#include "inner_api.h"
#include "CRDT.h"

using namespace CRDT;

//InnerAPI::InnerAPI(std::shared_ptr<CRDT::CRDTGraph> _G)
InnerAPI::InnerAPI(CRDT::CRDTGraph *G_)
{
    G = G_;
}

/// Computation of resultant RTMat going from A to common ancestor and from common ancestor to B (inverted)
std::optional<InnerAPI::Lists> InnerAPI::setLists(const std::string &destId, const std::string &origId)
{
    std::list<RMat::RTMat> listA, listB;

  	auto a = G->get_vertex(origId);
	auto b = G->get_vertex(destId);  
    if (!a.has_value() or !b.has_value())
		return {};
    
	int minLevel = std::min(a.value()->get_level().value_or(-1), b.value()->get_level().value_or(-1));
	while (a.value()->get_level() >= minLevel)
	{
        //qDebug() << "listaA" << a.value()->id() << a.value()->get_level().value() << a.value()->get_parent().value();
		auto p_node = G->get_vertex(a.value()->get_parent().value_or(-1));
      	if(!p_node.has_value())
			break;
        listA.push_back(p_node.value()->get_edge(a.value()->id(), "RT").value()->to_RT());   // the downwards RT link from parent to a
        a = p_node;
	}
	while (b.value()->get_level() >= minLevel)
	{
        //qDebug() << "listaB" << b.value()->id() << b.value()->get_level().value();
		auto p_node = G->get_vertex(b.value()->get_parent().value_or(-1));
		if(!p_node.has_value())
			break;
        listB.push_front(p_node.value()->get_edge(a.value()->id(), "RT").value()->to_RT());
        b = p_node;
	}
	// while (b->id() != a->id())  		// Estaba en InnerModel pero no sé bien cuándo hace falta
	// {
	// 	listA.push_back(a);
	// 	listB.push_front(b);
	// 	a = G->getNode(a->getParentId());
	// 	b = G->getNode(b->getParentId());
	// }
    return std::make_tuple(listA, listB);
}

////////////////////////////////////////////////////////////////////////////////////////
////// TRNASFORMATION MATRIX
////////////////////////////////////////////////////////////////////////////////////////

std::optional<RTMat> InnerAPI::getTransformationMatrixS(const std::string &dest, const std::string &orig)
{
	RTMat ret;
	// if (localHashTr.contains(QPair<QString, QString>(to, from)))
	// {
	// 	ret = localHashTr[QPair<QString, QString>(to, from)];
	// }
	// else
	// {

    auto lists = setLists(dest, orig);
	if(!lists.has_value())
		return {};
	auto &[listA, listB] = lists.value();
    
    for(auto &a: listA )
    {
	    ret = a*ret;
        //ret.print("ListA");
    }
    for(auto &b: listB )
    {
        ret = b.invert() * ret;
        //ret.print("ListB");
    }
    //	localHashTr[QPair<QString, QString>(to, from)] = ret;
    //}
	return ret;
}

std::optional<RTMat> InnerAPI::getTransformationMatrix(const QString &dest, const QString &orig)
{
	return getTransformationMatrixS(dest.toStdString(), orig.toStdString());
}

////////////////////////////////////////////////////////////////////////////////////////
////// TRNASFORM
////////////////////////////////////////////////////////////////////////////////////////
std::optional<QVec> InnerAPI::transformS(const std::string &destId, const QVec &initVec, const std::string &origId)
{
	if (initVec.size()==3)
	{
		auto tm = getTransformationMatrixS(destId, origId);
		if(tm.has_value())
			return (tm.value() * initVec.toHomogeneousCoordinates()).fromHomogeneousCoordinates();
		else
			return {};
	}
	else if (initVec.size()==6)
	{
		auto tm = getTransformationMatrixS(destId, origId);
		if(tm.has_value())
		{
			const QMat M = tm.value();
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
		else
		return {};
	}
	else
		return {};
};

 std::optional<QVec> InnerAPI::transformS( const std::string &destId, const std::string &origId)
 {
	return transformS(destId, QVec::vec3(0.,0.,0.), origId);
 }

 std::optional<QVec> InnerAPI::transform(const QString & destId, const QVec &origVec, const QString & origId)
 {
	return transformS(destId.toStdString(), origVec, origId.toStdString());
 }

 std::optional<QVec> InnerAPI::transform( const QString &destId, const QString & origId)
 {
 	return transformS(destId.toStdString(), QVec::vec3(0.,0.,0.), origId.toStdString());
 }

std::optional<QVec> InnerAPI::transform6D( const QString &destId, const QString & origId)
 {
	return transformS(destId.toStdString(), QVec::vec6(0,0,0,0,0,0), origId.toStdString());
 }

std::optional<QVec> InnerAPI::transform6D( const QString &destId, const QVec &origVec, const QString & origId)
 {
	Q_ASSERT(origVec.size() == 6); 
	return transformS(destId.toStdString(), origVec, origId.toStdString());
 }

 std::optional<QVec> InnerAPI::transformS6D( const std::string &destId, const QVec &origVec, const std::string& origId)
 {
	Q_ASSERT(origVec.size() == 6); 
	return transformS(destId, origVec, origId);
 }

 std::optional<QVec> InnerAPI::transformS6D( const std::string &destId, const std::string & origId)
 {
	return transformS(destId, QVec::vec6(0,0,0,0,0,0), origId);
 }