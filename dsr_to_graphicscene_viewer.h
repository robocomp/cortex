/*
 * Copyright 2018 <copyright holder> <email>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
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

#ifndef DSR_TO_GRAPHCISCENE_VIEWER_H
#define DSR_TO_GRAPHCISCENE_VIEWER_H

#include <chrono>
#include <QWidget>
#include <QApplication>
#include <QDesktopWidget>
#include <QGLWidget>
#include <QHBoxLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QResizeEvent>
#include "CRDT.h"

namespace DSR
{

    class DSRtoGraphicsceneViewer : public QWidget
    {
        Q_OBJECT
        public:
            DSRtoGraphicsceneViewer(std::shared_ptr<CRDT::CRDTGraph> G_, float scaleX, float scaleY, QWidget *parent=0);
//            void add_plane();
//            void add_mesh();
//            void add_person();

        public slots:   // From G
            void add_or_assign_node_slot(const std::int32_t id, const std::string &type);
            void add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& type);
//            void updateX();
            
        protected:  

//            virtual void paintGL();
//            virtual void initializeGL();
//            virtual void mouseMoveEvent(QMouseEvent* event);        
//            virtual void mousePressEvent(QMouseEvent* event);
//            virtual void mouseReleaseEvent(QMouseEvent* event);
            virtual void wheelEvent(QWheelEvent* event);
            virtual void resizeEvent(QResizeEvent *e); 
//            virtual bool event(QEvent* event);

        private:
            std::shared_ptr<CRDT::CRDTGraph> G;
            std::unique_ptr<CRDT::InnerAPI> innermodel;      
            
            qreal m_scaleX, m_scaleY;
            QGraphicsScene scene;
            QGraphicsView *view;
            //Hashes

            void createGraph();

            void add_or_assign_box(Node &node);
            void add_or_assign_mesh(Node &node);
    };
};
#endif

