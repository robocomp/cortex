#ifndef INNER_API
#define INNER_API

#include <qmat/QMatAll>
#include "CRDT.h"

namespace CRDT
{

    class CRDTGraph;

    class InnerAPI
    {
        using ListsPtr = std::tuple<std::list<Edge>, std::list<Edge> >; 
        public:
            InnerAPI(CRDTGraph *G_);
            
            /////////////////////////////////////////////////
            /// Kinematic transformation methods
            ////////////////////////////////////////////////
            QVec transform(const QString & destId, const QVec &origVec, const QString & origId);
            QVec transform( const QString &destId, const QString & origId);
            QVec transformS( const std::string &destId, const QVec &origVec, const std::string & origId);
            QVec transformS( const std::string &destId, const std::string &origId);
            QVec transform6D(const QString &destId, const QVec &origVec, const QString & origId); 
            QVec transform6D(const QString &destId, const QString & origId);
            QVec transformS6D(const std::string &destId, const std::string & origId);
            QVec transformS6D(const std::string &destId, const QVec &origVec, const std::string & origId);
                
            ////////////////////////////////////////////////
            /// Transformation matrix retrieval methods
            ////////////////////////////////////////////////
            RTMat getTransformationMatrix(const QString &destId, const QString &origId);
            RTMat getTransformationMatrixS(const std::string &destId, const std::string &origId);
            QMat getRotationMatrixTo(const QString &to, const QString &from);
            QVec getTranslationVectorTo(const QString &to, const QString &from);
            QVec rotationAngles(const QString & destId, const QString & origId);
        
        private:
            //std::shared_ptr<CRDT::CRDTGraph> G;
            CRDT::CRDTGraph *G;
            ListsPtr setLists(const std::string &origId, const std::string &destId);
        
    };
};

#endif