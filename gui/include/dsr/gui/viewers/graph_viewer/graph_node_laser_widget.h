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

#ifndef GRAPHNODELASERWIDGET_H
#define GRAPHNODELASERWIDGET_H

#include <QDebug>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QVector2D>
#include <QVector3D>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <dsr/api/dsr_api.h>
#include <dsr/gui/dsr_gui.h>

class GraphNodeLaserWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

  public:
    GraphNodeLaserWidget(std::shared_ptr<DSR::DSRGraph> graph_, std::uint64_t node_id_) : graph(std::move(graph_)), node_id(node_id_)
    {
        resize(640, 640);
        setWindowTitle("Laser data");
        setFocusPolicy(Qt::StrongFocus);

        QObject::connect(graph.get(), &DSR::DSRGraph::update_node_signal,
                         this, &GraphNodeLaserWidget::onNodeUpdated, Qt::QueuedConnection);
        QObject::connect(graph.get(), &DSR::DSRGraph::update_node_attr_signal,
                         this, &GraphNodeLaserWidget::onNodeAttrsUpdated, Qt::QueuedConnection);

        updatePointCloud();
        show();
    }

    ~GraphNodeLaserWidget() override
    {
        if(context())
        {
            makeCurrent();
            if(points_vbo.isCreated()) points_vbo.destroy();
            if(axis_vbo.isCreated()) axis_vbo.destroy();
            if(vao.isCreated()) vao.destroy();
            doneCurrent();
        }
    }

    void closeEvent(QCloseEvent *event) override
    {
        (void)event;
        disconnect(graph.get(), nullptr, this, nullptr);
    }

  public slots:
    void onNodeUpdated(std::uint64_t id, const std::string &type)
    {
        (void)type;
        if(id != node_id)
            return;
        maybeUpdatePointCloud();
    }

    void onNodeAttrsUpdated(std::uint64_t id, const std::vector<std::string> &attrs)
    {
        if(id != node_id)
            return;

        const bool relevant = std::any_of(attrs.begin(), attrs.end(), [](const std::string &a)
        {
            return a == laser_X_att::attr_name || a == laser_Y_att::attr_name || a == laser_Z_att::attr_name;
        });

        if(relevant || attrs.empty())
            maybeUpdatePointCloud();
    }

  protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);

        if(!program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
            #version 330 core
            layout(location = 0) in vec3 position;
            uniform mat4 u_mvp;
            uniform float u_point_size;
            void main()
            {
                gl_Position = u_mvp * vec4(position, 1.0);
                gl_PointSize = u_point_size;
            }
        )"))
            qWarning() << "Laser viewer vertex shader error:" << program.log();

        if(!program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
            #version 330 core
            out vec4 fragColor;
            uniform vec3 u_color;
            void main()
            {
                fragColor = vec4(u_color, 1.0);
            }
        )"))
            qWarning() << "Laser viewer fragment shader error:" << program.log();

        if(!program.link())
            qWarning() << "Laser viewer shader link error:" << program.log();

        u_mvp_loc = program.uniformLocation("u_mvp");
        u_color_loc = program.uniformLocation("u_color");
        u_point_size_loc = program.uniformLocation("u_point_size");

        vao.create();
        points_vbo.create();
        axis_vbo.create();

        updateGpuBuffers();
    }

    void resizeGL(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }

    void paintGL() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if(!program.isLinked())
            return;

        const float aspect = static_cast<float>(std::max(1, width())) / static_cast<float>(std::max(1, height()));

        QMatrix4x4 proj;
        const float far_plane = std::max(10000.0f, cam_dist * 20.0f + cloud_radius * 20.0f);
        proj.perspective(45.0f, aspect, 0.01f, far_plane);

        QMatrix4x4 view;
        view.translate(pan_x, pan_y, -cam_dist);
        view.rotate(pitch_deg, 1.0f, 0.0f, 0.0f);
        view.rotate(yaw_deg, 0.0f, 1.0f, 0.0f);
        view.translate(-center_x, -center_y, -center_z);

        const QMatrix4x4 mvp = proj * view;

        program.bind();
        program.setUniformValue(u_mvp_loc, mvp);

        vao.bind();

        if(!axis_vertices.empty())
        {
            axis_vbo.bind();
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            program.setUniformValue(u_color_loc, QVector3D(0.35f, 0.45f, 0.90f));
            program.setUniformValue(u_point_size_loc, 1.0f);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(axis_vertices.size()));
            axis_vbo.release();
        }

        if(!point_vertices.empty())
        {
            points_vbo.bind();
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            program.setUniformValue(u_color_loc, QVector3D(0.95f, 0.74f, 0.18f));
            program.setUniformValue(u_point_size_loc, has_z_data ? 3.0f : 4.0f);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(point_vertices.size()));
            points_vbo.release();
        }

        glDisableVertexAttribArray(0);
        vao.release();
        program.release();

        // 2D HUD overlay with current rendered point count.
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(QColor(255, 255, 255));
        const QString fps_text = (display_fps_hz > 0.0f)
                                     ? QString::number(display_fps_hz, 'f', 1)
                                     : QStringLiteral("--");
        painter.drawText(QRect(10, 10, width() - 20, 24),
                         Qt::AlignLeft | Qt::AlignTop,
                         QString("Points: %1    FPS: %2")
                             .arg(static_cast<qulonglong>(point_vertices.size()))
                             .arg(fps_text));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        last_mouse_pos = event->pos();
        if(event->button() == Qt::LeftButton)
        {
            rotating = true;
            user_interacted = true;
        }
        else if(event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
        {
            panning = true;
            user_interacted = true;
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint delta = event->pos() - last_mouse_pos;
        last_mouse_pos = event->pos();

        if(rotating)
        {
            yaw_deg += static_cast<float>(delta.x()) * 0.4f;
            pitch_deg += static_cast<float>(delta.y()) * 0.4f;
            pitch_deg = std::clamp(pitch_deg, -89.0f, 89.0f);
            update();
        }
        else if(panning)
        {
            const float scale = std::max(0.0005f, cam_dist * 0.0010f);
            pan_x += static_cast<float>(delta.x()) * scale;
            pan_y -= static_cast<float>(delta.y()) * scale;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        (void)event;
        rotating = false;
        panning = false;
    }

    void wheelEvent(QWheelEvent *event) override
    {
        // QGLViewer-like wheel response: perceptible zoom per wheel notch.
        const QVector2D num_steps(static_cast<float>(event->angleDelta().x()) / 120.f,
                                  static_cast<float>(event->angleDelta().y()) / 120.f);
        if(std::abs(num_steps.y()) < 1e-4f)
            return;

        const float old_cam_dist = cam_dist;
        const float zoom_per_notch = 0.85f;
        const float factor = std::pow(zoom_per_notch, num_steps.y());
        cam_dist *= factor;
        cam_dist = std::clamp(cam_dist, 0.005f, 1000000.0f);

        // Zoom to cursor: keep the point under the mouse more stable while zooming.
        const QPointF p = event->position();
        const float nx = static_cast<float>((p.x() / std::max(1, width())) - 0.5);
        const float ny = static_cast<float>(0.5 - (p.y() / std::max(1, height())));
        const float zoom_delta = (old_cam_dist - cam_dist);
        const float cursor_zoom_gain = 0.9f;
        pan_x += nx * zoom_delta * cursor_zoom_gain;
        pan_y += ny * zoom_delta * cursor_zoom_gain;

        user_interacted = true;
        update();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if(event->key() == Qt::Key_R)
        {
            resetView();
            update();
            return;
        }
        QOpenGLWidget::keyPressEvent(event);
    }

  private:
    std::shared_ptr<DSR::DSRGraph> graph;
    std::uint64_t node_id;

    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    std::vector<QVector3D> point_vertices;
    std::vector<QVector3D> axis_vertices;

    bool has_z_data = false;
    bool warned_missing_z = false;
    bool warned_mismatch_z = false;

    float center_x = 0.f;
    float center_y = 0.f;
    float center_z = 0.f;
    float cloud_radius = 1.0f;

    float cam_dist = 5.0f;
    float yaw_deg = 25.0f;
    float pitch_deg = -20.0f;
    float pan_x = 0.f;
    float pan_y = 0.f;

    bool rotating = false;
    bool panning = false;
    QPoint last_mouse_pos;

    QOpenGLShaderProgram program;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer points_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer axis_vbo{QOpenGLBuffer::VertexBuffer};
    int u_mvp_loc = -1;
    int u_color_loc = -1;
    int u_point_size_loc = -1;

    void maybeUpdatePointCloud()
    {
        const auto now = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update) < std::chrono::milliseconds(20))
            return;

        last_update = now;
        updatePointCloud();
    }

    void updatePointCloud()
    {
        try
        {
            const std::optional<Node> n = graph->get_node(node_id);
            if(!n.has_value())
            {
                point_vertices.clear();
                axis_vertices = makeAxes(1.0f);
                updateGpuBuffers();
                update();
                return;
            }

            const auto lx = graph->get_attrib_by_name<laser_X_att>(n.value());
            const auto ly = graph->get_attrib_by_name<laser_Y_att>(n.value());
            const auto lz = graph->get_attrib_by_name<laser_Z_att>(n.value());
            const auto lts = graph->get_attrib_by_name<laser_timestamp_att>(n.value());

            if(!lx.has_value() || !ly.has_value())
            {
                point_vertices.clear();
                axis_vertices = makeAxes(1.0f);
                updateGpuBuffers();
                update();
                return;
            }

            const auto &xs = lx.value().get();
            const auto &ys = ly.value().get();
            const bool z_available = lz.has_value();
            const std::vector<float> *zs = z_available ? &lz.value().get() : nullptr;

            const std::size_t nxy = std::min(xs.size(), ys.size());
            if(nxy == 0)
            {
                point_vertices.clear();
                axis_vertices = makeAxes(1.0f);
                updateGpuBuffers();
                update();
                return;
            }

            std::size_t npts = nxy;
            has_z_data = false;
            if(z_available)
            {
                npts = std::min(nxy, zs->size());
                has_z_data = (zs->size() >= npts) && (npts > 0);
                if(zs->size() != nxy && !warned_mismatch_z)
                {
                    qWarning() << "GraphNodeLaserWidget: laser_Z size differs from laser_X/laser_Y, using min size";
                    warned_mismatch_z = true;
                }
            }
            else if(!warned_missing_z)
            {
                qInfo() << "GraphNodeLaserWidget: laser_Z not found, drawing 2D cloud on Z=0";
                warned_missing_z = true;
            }

            point_vertices.clear();
            point_vertices.reserve(npts);

            // Prefer producer timestamp to estimate incoming lidar FPS in the popup.
            bool fps_updated = false;
            if(lts.has_value())
            {
                const std::uint64_t current_ts_ms = static_cast<std::uint64_t>(lts.value());
                if(last_laser_timestamp_ms > 0 && current_ts_ms > last_laser_timestamp_ms)
                {
                    const std::uint64_t dt_ms = current_ts_ms - last_laser_timestamp_ms;
                    if(dt_ms > 0 && dt_ms < 10000)
                    {
                        const float inst_fps = 1000.0f / static_cast<float>(dt_ms);
                        display_fps_hz = (display_fps_hz > 0.0f) ? (0.85f * display_fps_hz + 0.15f * inst_fps) : inst_fps;
                        fps_updated = true;
                    }
                }
                last_laser_timestamp_ms = current_ts_ms;
            }

            if(!fps_updated)
            {
                const auto now = std::chrono::steady_clock::now();
                if(have_last_arrival_time)
                {
                    const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_arrival_time).count();
                    if(dt_ms > 0 && dt_ms < 10000)
                    {
                        const float inst_fps = 1000.0f / static_cast<float>(dt_ms);
                        display_fps_hz = (display_fps_hz > 0.0f) ? (0.85f * display_fps_hz + 0.15f * inst_fps) : inst_fps;
                    }
                }
                last_arrival_time = now;
                have_last_arrival_time = true;
            }

            float minx = std::numeric_limits<float>::max();
            float miny = std::numeric_limits<float>::max();
            float minz = std::numeric_limits<float>::max();
            float maxx = std::numeric_limits<float>::lowest();
            float maxy = std::numeric_limits<float>::lowest();
            float maxz = std::numeric_limits<float>::lowest();

            for(std::size_t i = 0; i < npts; ++i)
            {
                const float x = xs[i];
                const float y = ys[i];
                const float z = has_z_data ? (*zs)[i] : 0.f;

                if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                    continue;

                point_vertices.emplace_back(x, y, z);
                minx = std::min(minx, x); maxx = std::max(maxx, x);
                miny = std::min(miny, y); maxy = std::max(maxy, y);
                minz = std::min(minz, z); maxz = std::max(maxz, z);
            }

            if(point_vertices.empty())
            {
                axis_vertices = makeAxes(1.0f);
                updateGpuBuffers();
                update();
                return;
            }

            center_x = (minx + maxx) * 0.5f;
            center_y = (miny + maxy) * 0.5f;
            center_z = (minz + maxz) * 0.5f;

            const float rx = std::max(0.001f, (maxx - minx) * 0.5f);
            const float ry = std::max(0.001f, (maxy - miny) * 0.5f);
            const float rz = std::max(0.001f, (maxz - minz) * 0.5f);
            cloud_radius = std::max({rx, ry, rz});
            axis_vertices = makeAxes(cloud_radius);

            if(!user_interacted)
                cam_dist = std::max(2.0f, cloud_radius * 3.0f);

            updateGpuBuffers();
            update();
        }
        catch(const std::exception &e)
        {
            qWarning() << "GraphNodeLaserWidget update error:" << e.what();
        }
    }

    void updateGpuBuffers()
    {
        if(!context() || !points_vbo.isCreated() || !axis_vbo.isCreated())
            return;

        makeCurrent();

        points_vbo.bind();
        points_vbo.allocate(point_vertices.data(), static_cast<int>(point_vertices.size() * sizeof(QVector3D)));
        points_vbo.release();

        axis_vbo.bind();
        axis_vbo.allocate(axis_vertices.data(), static_cast<int>(axis_vertices.size() * sizeof(QVector3D)));
        axis_vbo.release();

        doneCurrent();
    }

    std::vector<QVector3D> makeAxes(float scale) const
    {
        const float s = std::max(0.5f, scale);
        std::vector<QVector3D> lines;
        lines.reserve(6);

        lines.emplace_back(center_x, center_y, center_z);
        lines.emplace_back(center_x + s, center_y, center_z);

        lines.emplace_back(center_x, center_y, center_z);
        lines.emplace_back(center_x, center_y + s, center_z);

        lines.emplace_back(center_x, center_y, center_z);
        lines.emplace_back(center_x, center_y, center_z + s);

        return lines;
    }

    void resetView()
    {
        yaw_deg = 25.0f;
        pitch_deg = -20.0f;
        pan_x = 0.f;
        pan_y = 0.f;
        cam_dist = std::max(2.0f, cloud_radius * 3.0f);
        user_interacted = false;
    }

    bool user_interacted = false;
    float display_fps_hz = 0.0f;
    std::uint64_t last_laser_timestamp_ms = 0;
    std::chrono::steady_clock::time_point last_arrival_time{};
    bool have_last_arrival_time = false;
};

#endif // GRAPHNODELASERWIDGET_H
