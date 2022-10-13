/*
 * Copyright 2022 <copyright holder> <email>
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

#ifndef DSR_QT3D_VIEWER_H
#define DSR_QT3D_VIEWER_H

#include <chrono>


#include <Qt3DCore/QEntity>
#include <Qt3DRender/QCamera>
#include <Qt3DRender/QCameraLens>
#include <Qt3DExtras/Qt3DExtras>
#include <Qt3DInput/Qt3DInput>
#include <Qt3DCore/Qt3DCore>

#include <QObject>
#include <QWidget>
#include <QMatrix4x4>

#include <cstdint>
#include <dsr/api/dsr_api.h>
#include <qwidget.h>

using namespace std::chrono_literals;

namespace DSR
{

    class QT3DViewer : public QObject
    {
    Q_OBJECT
        public:

        explicit QT3DViewer(std::shared_ptr<DSR::DSRGraph> g_);
        ~QT3DViewer() override;

        QWidget *create_widget();
        QWidget *get_widget();

        void show();

        private:

        void initialize();

        void update_edge(uint64_t from, uint64_t to, const std::string& type);
        void delete_edge(uint64_t from, uint64_t to, const std::string& type);
        //void update_edge_attr(uint64_t from, uint64_t to, const std::string& type, const std::vector<std::string> &att_names);

        void update_qt3d_entity(const Node& node);
        void delete_node(uint64_t id);
        void update_node(uint64_t id, const std::string& type);
        //void update_node_attr(uint64_t id, const std::vector<std::string> &att_names);


        //Convenience methods

        Qt3DRender::QMaterial *color_material(QColor color)
        {
            auto *mat = new Qt3DExtras::QDiffuseSpecularMaterial(); 
            mat->setAmbient(color);
            mat->setSpecular(QColor(0, 0, 0));
            qInfo() << "Color: " << color;
            return mat;
        };

        bool only_one_widget;
        Qt3DExtras::Qt3DWindow *view; //We don't manage this pointer object. A widget will take it's ownership.
        QWidget * widget;
        std::shared_ptr<DSR::DSRGraph> g; //We don't own this pointer.
        std::unique_ptr<DSR::InnerEigenAPI> inner;
        Qt3DCore::QEntity *rootEntity;
        Qt3DRender::QLayer* globalLayer;
        std::unordered_map<uint64_t, Qt3DCore::QEntity*> entities;

    };
};
#endif