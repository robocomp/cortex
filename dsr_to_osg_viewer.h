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

#ifndef DSR_TO_OSG_VIEWER_H
#define DSR_TO_OSG_VIEWER_H

#include <chrono>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>
#include <QOpenGLWidget>
#include "CRDT.h"

using namespace std::chrono_literals;

namespace DSR
{

    class DSRtoOSGViewer : public QOpenGLWidget
    {
        public:
            DSRtoOSGViewer(std::shared_ptr<CRDT::CRDTGraph> G, float scaleX, float scaleY, QWidget *parent=0);
            void add_cylinder();
    
        public slots:
            // define slots to recoeve signals from G
            // create a plane, mesh, move

            
        protected:  
            virtual void paintGL();
            virtual void resizeGL( int width, int height );
            virtual void initializeGL();
            virtual void mouseMoveEvent(QMouseEvent* event);        
            virtual void mousePressEvent(QMouseEvent* event);
            virtual void mouseReleaseEvent(QMouseEvent* event);
            virtual void wheelEvent(QWheelEvent* event);
            virtual bool event(QEvent* event);

        private:
            std::shared_ptr<CRDT::CRDTGraph> G;
            osgGA::EventQueue* getEventQueue() const ;
            osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> _mGraphicsWindow;
            osg::ref_ptr<osgViewer::Viewer> _mViewer;
            qreal m_scaleX, m_scaleY;
            osg::ref_ptr<osg::Group> root;

        public slots:
            void add_or_assign_node_slot();
            void add_or_assign_edge_slot();
    };
};
#endif

//class IMVPlane : public osg::Geode
// {
//     public: 
//         IMVPlane(std::string imagenEntrada, osg::Vec4 valoresMaterial, float transparencia) : osg::Geode()
//         {
//             data = NULL;
//             bool constantColor = false;
//             if (imagenEntrada.size() == 7)
//                 if (imagenEntrada[0] == '#')
//                     constantColor = true;
//             // Open image
//             image = NULL;
//             osg::ref_ptr<osg::TessellationHints> hints;
//             if (imagenEntrada.size()>0 and not constantColor)
//             {
//                 if (imagenEntrada == "custom")
//                     image = new osg::Image();
//                 else
//                 {
//                     image = osgDB::readImageFile(imagenEntrada);
//                     if (not image)
//                     {
//                         qDebug() << "Couldn't load texture:" << imagenEntrada.c_str();
//                         throw "Couldn't load texture.";
//                     }
//                 }
//             }
//             hints = new osg::TessellationHints;
//             hints->setDetailRatio(2.0f);
//             //osg::ref_ptr<osg::Box> myBox = new osg::Box(QVecToOSGVec(QVec::vec3(0,0,0)), plane->width, -plane->height, plane->depth);
//             osg::ref_ptr<osg::Box> myBox = new osg::Box(QVecToOSGVec(QVec::vec3(0,0,0)), 10, -10, 1);
//             planeDrawable = new osg::ShapeDrawable(myBox, hints);
//             planeDrawable->setColor(htmlStringToOsgVec4(QString::fromStdString(imagenEntrada)));
//             addDrawable(planeDrawable);
//             if (not constantColor)
//             {
//                 // Texture
//                 texture = new osg::Texture2D;
//                 if (image)
//                 {
//                     texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
//                     texture->setWrap(osg::Texture::WRAP_R, osg::Texture::REPEAT);
//                     texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
//                     texture->setImage(image);
//                     texture->setDataVariance(Object::DYNAMIC);
//                     texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
//                     texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
//                     texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
//                     texture->setWrap(osg::Texture::WRAP_R, osg::Texture::REPEAT);
//                     texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
//                     texture->setTextureWidth(1);
//                     texture->setTextureHeight(1);
//                 }
//                 texture->setResizeNonPowerOfTwoHint(false);
//                 // Material
//                 osg::ref_ptr<osg::Material> material = new osg::Material();
//                 material->setTransparency( osg::Material::FRONT_AND_BACK, transparencia);
//                 material->setEmission(osg::Material::FRONT, osg::Vec4(0.8, 0.8, 0.8, 0.5));
//                 // Assign the material and texture to the plane
//                 osg::StateSet *sphereStateSet = getOrCreateStateSet();
//                 sphereStateSet->ref();
//                 sphereStateSet->setAttribute(material);
//         #ifdef __arm__
//         #else
//                 sphereStateSet->setTextureMode(0, GL_TEXTURE_GEN_R, osg::StateAttribute::ON);
//         #endif
//                 sphereStateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
//             }
//         }

//         void setImage(osg::Image *image_)
//         {
//             texture = new osg::Texture2D;
//             image = image_;
//             if (image)
//             {
//                 texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
//                 texture->setWrap(osg::Texture::WRAP_R, osg::Texture::REPEAT);
//                 texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
//                 texture->setImage(image);
//                 texture->setDataVariance(Object::DYNAMIC);
//                 texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
//                 texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
//                 texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
//                 texture->setWrap(osg::Texture::WRAP_R, osg::Texture::REPEAT);
//                 texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
//                 texture->setTextureWidth(1);
//                 texture->setTextureHeight(1);
//             }
//             texture->setResizeNonPowerOfTwoHint(false);
//             // Material
//             osg::ref_ptr<osg::Material> material = new osg::Material();
//             material->setTransparency( osg::Material::FRONT_AND_BACK, 0);
//             material->setEmission(osg::Material::FRONT, osg::Vec4(0.8, 0.8, 0.8, 0.5));
//             // Assign the material and texture to the plane
//             osg::StateSet *sphereStateSet = getOrCreateStateSet();
//             sphereStateSet->ref();
//             sphereStateSet->setAttribute(material);
//             #ifdef __arm__
//             #else
//                     sphereStateSet->setTextureMode(0, GL_TEXTURE_GEN_R, osg::StateAttribute::ON);
//             #endif
//                     sphereStateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
//         }

//         void updateBuffer(uint8_t *data_, int32_t width_, int32_t height_)
//         {
//             data = data_;
//             width = width_;
//             height = height_;
//             dirty = true;
//         }

//         void performUpdate()
//         {
//             static uint8_t *backData = NULL;
//             if (dirty)
//             {
//                 if (backData != data)
//                 {
//                     #ifdef __arm__
//                                 image->setImage(width, height, 3, GL_RGB, GL_RGB, GL_UNSIGNED_BYTE, data, osg::Image::NO_DELETE, 1);
//                     #else
//                                 image->setImage(width, height, 3, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, data, osg::Image::NO_DELETE, 1);
//                     #endif
//                 }
//                 else
//                     image->dirty();
//                 dirty = false;
//                 backData = data;
//             }
//         }

//         osg::Vec3 QVecToOSGVec(const QVec &vec)
//         {
// 	        return osg::Vec3(vec(0), vec(1), -vec(2));
//         }

//         osg::Vec4 htmlStringToOsgVec4(QString color)
//         {
//             QString red   = QString("00");
//             QString green = QString("00");
//             QString blue  = QString("00");
//             bool ok;
//             red[0]   = color[1]; red[1]   = color[2];
//             green[0] = color[3]; green[1] = color[4];
//             blue[0]  = color[5]; blue[1]  = color[6];
//             return osg::Vec4(float(red.toInt(&ok, 16))/255., float(green.toInt(&ok, 16))/255., float(blue.toInt(&ok, 16))/255., 0.f);
//         }

//         uint8_t *data;
//         bool dirty;
//         int32_t width, height;
//         osg::ref_ptr<osg::Texture2D> texture;
//         osg::ref_ptr<osg::ShapeDrawable> planeDrawable;
//         osg::ref_ptr<osg::Image> image;
// };
