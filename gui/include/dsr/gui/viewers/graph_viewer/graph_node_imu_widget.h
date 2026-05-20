/*
 * Copyright 2026
 */

#ifndef GRAPHNODEIMUWIDGET_H
#define GRAPHNODEIMUWIDGET_H

#include <QCloseEvent>
#include <QColor>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QRect>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <dsr/api/dsr_api.h>

class GraphNodeIMUWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

  public:
    GraphNodeIMUWidget(std::shared_ptr<DSR::DSRGraph> graph_, std::uint64_t node_id_)
        : graph(std::move(graph_)), node_id(node_id_)
    {
        resize(920, 620);
        setWindowTitle("IMU data");
        setFocusPolicy(Qt::StrongFocus);

        QObject::connect(graph.get(), &DSR::DSRGraph::update_node_signal,
                         this, &GraphNodeIMUWidget::onNodeUpdated, Qt::QueuedConnection);
        QObject::connect(graph.get(), &DSR::DSRGraph::update_node_attr_signal,
                         this, &GraphNodeIMUWidget::onNodeAttrsUpdated, Qt::QueuedConnection);

        updateSeries();
        show();
    }

    ~GraphNodeIMUWidget() override
    {
        if(context())
        {
            makeCurrent();
            if(vbo.isCreated()) vbo.destroy();
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
        maybeUpdateSeries();
    }

    void onNodeAttrsUpdated(std::uint64_t id, const std::vector<std::string> &attrs)
    {
        if(id != node_id)
            return;

        const bool relevant = std::any_of(attrs.begin(), attrs.end(), [](const std::string &a)
        {
            return a == imu_accelerometer_att::attr_name
                || a == imu_gyroscope_att::attr_name
                || a == imu_angular_euler_xyz_pose_att::attr_name
                || a == imu_compass_att::attr_name
                || a == imu_time_stamp_att::attr_name;
        });

        if(relevant || attrs.empty())
            maybeUpdateSeries();
    }

  protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);

        if(!program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
            #version 330 core
            layout(location = 0) in vec2 position;
            layout(location = 1) in vec3 color;
            out vec3 v_color;
            void main()
            {
                gl_Position = vec4(position, 0.0, 1.0);
                v_color = color;
            }
        )"))
            qWarning() << "IMU viewer vertex shader error:" << program.log();

        if(!program.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
            #version 330 core
            in vec3 v_color;
            out vec4 fragColor;
            void main()
            {
                fragColor = vec4(v_color, 1.0);
            }
        )"))
            qWarning() << "IMU viewer fragment shader error:" << program.log();

        if(!program.link())
            qWarning() << "IMU viewer shader link error:" << program.log();

        vao.create();
        vao.bind();
        vbo.create();
        vbo.bind();
        vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        program.bind();
        program.enableAttributeArray(0);
        program.enableAttributeArray(1);
        program.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, x), 2, sizeof(Vertex));
        program.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, r), 3, sizeof(Vertex));
        program.release();
        vbo.release();
        vao.release();

        updateGpuBuffers();
    }

    void resizeGL(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }

    void paintGL() override
    {
        glClear(GL_COLOR_BUFFER_BIT);
        if(!program.isLinked())
            return;

        program.bind();
        vao.bind();

        if(!vertices.empty())
        {
            vbo.bind();
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, r)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);

            if(grid_vertex_count > 0)
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(grid_vertex_count));

            std::size_t offset = grid_vertex_count;
            for(const auto &plot : plots)
            {
                for(const auto &channel : plot.channels)
                {
                    const auto n = static_cast<GLsizei>(channel.values.size());
                    if(n >= 2)
                        glDrawArrays(GL_LINE_STRIP, static_cast<GLint>(offset), n);
                    offset += channel.values.size();
                }
            }

            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            vbo.release();
        }

        vao.release();
        program.release();

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(QColor(240, 240, 240));
        painter.drawText(QRect(10, 10, width() - 20, 24),
                         Qt::AlignLeft | Qt::AlignTop,
                         QString("IMU samples: %1    Last ts: %2")
                             .arg(static_cast<int>(sample_count))
                             .arg(last_timestamp_ms > 0 ? QString::number(last_timestamp_ms) : QStringLiteral("--")));

        const auto layout = computePlotLayout();
        for(std::size_t plot_index = 0; plot_index < plots.size(); ++plot_index)
        {
            const auto &plot = plots[plot_index];
            const auto &plot_layout = layout[plot_index];
            painter.setPen(QColor(210, 210, 210));
            painter.drawText(plot_layout.title_rect, Qt::AlignLeft | Qt::AlignVCenter,
                             QString::fromStdString(plot.title));
            for(std::size_t channel_index = 0; channel_index < plot.channels.size(); ++channel_index)
            {
                const auto &channel = plot.channels[channel_index];
                painter.setPen(channel.color);
                painter.drawText(plot_layout.value_rects[channel_index], Qt::AlignLeft | Qt::AlignVCenter,
                                 QString("%1: %2")
                                     .arg(QString::fromStdString(channel.label))
                                     .arg(channel.values.empty() ? QStringLiteral("--") : QString::number(channel.values.back(), 'f', 4)));
            }
        }
    }

  private:
    struct Vertex
    {
        float x, y;
        float r, g, b;
    };

    struct Channel
    {
        std::string label;
        QColor color;
        std::deque<float> values;
    };

    struct Plot
    {
        std::string title;
        std::array<Channel, 3> channels;
    };

    struct PlotLayout
    {
        QRect title_rect;
        std::array<QRect, 3> value_rects;
        float plot_top = 0.0f;
        float plot_bottom = 0.0f;
        float plot_mid = 0.0f;
        float data_left = 0.0f;
        float data_right = 0.0f;
    };

    static constexpr std::size_t history_size = 360;
    std::shared_ptr<DSR::DSRGraph> graph;
    std::uint64_t node_id;
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    std::uint64_t last_timestamp_ms = 0;
    std::size_t sample_count = 0;

    std::array<Plot, 3> plots{{
        Plot{"Acceleration", {Channel{"ax", QColor(255, 159, 28), {}}, Channel{"ay", QColor(46, 204, 113), {}}, Channel{"az", QColor(52, 152, 219), {}}}},
        Plot{"Angular velocity", {Channel{"gx", QColor(231, 76, 60), {}}, Channel{"gy", QColor(155, 89, 182), {}}, Channel{"gz", QColor(241, 196, 15), {}}}},
        Plot{"Euler xyz pose", {Channel{"roll", QColor(26, 188, 156), {}}, Channel{"pitch", QColor(230, 126, 34), {}}, Channel{"yaw", QColor(236, 240, 241), {}}}}
    }};

    std::vector<Vertex> vertices;
    std::size_t grid_vertex_count = 0;

    QOpenGLShaderProgram program;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};

    void maybeUpdateSeries()
    {
        const auto now = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update) < std::chrono::milliseconds(20))
            return;

        last_update = now;
        updateSeries();
    }

    void updateSeries()
    {
        try
        {
            const auto n = graph->get_node(node_id);
            if(!n.has_value())
            {
                clearHistory();
                rebuildVertices();
                updateGpuBuffers();
                update();
                return;
            }

            const auto acc_opt = graph->get_attrib_by_name<imu_accelerometer_att>(n.value());
            const auto gyro_opt = graph->get_attrib_by_name<imu_gyroscope_att>(n.value());
            const auto euler_opt = graph->get_attrib_by_name<imu_angular_euler_xyz_pose_att>(n.value());
            const auto ts_opt = graph->get_attrib_by_name<imu_time_stamp_att>(n.value());

            if(!acc_opt.has_value() || !gyro_opt.has_value() || !euler_opt.has_value() || !ts_opt.has_value())
            {
                rebuildVertices();
                updateGpuBuffers();
                update();
                return;
            }

            const auto &acc = acc_opt.value().get();
            const auto &gyro = gyro_opt.value().get();
            const auto &euler = euler_opt.value().get();
            const std::uint64_t ts = static_cast<std::uint64_t>(ts_opt.value());

            if(acc.size() < 3 || gyro.size() < 3 || euler.size() < 3)
            {
                rebuildVertices();
                updateGpuBuffers();
                update();
                return;
            }

            if(ts != 0 && ts != last_timestamp_ms)
            {
                appendSample(plots[0], acc[0], acc[1], acc[2]);
                appendSample(plots[1], gyro[0], gyro[1], gyro[2]);
                appendSample(plots[2], euler[0], euler[1], euler[2]);
                last_timestamp_ms = ts;
                sample_count = std::min<std::size_t>(history_size, sample_count + 1);
            }

            rebuildVertices();
            updateGpuBuffers();
            update();
        }
        catch(const std::exception &e)
        {
            qWarning() << "GraphNodeIMUWidget update error:" << e.what();
        }
    }

    void appendSample(Plot &plot, float a, float b, float c)
    {
        pushValue(plot.channels[0].values, a);
        pushValue(plot.channels[1].values, b);
        pushValue(plot.channels[2].values, c);
    }

    static void pushValue(std::deque<float> &history, float value)
    {
        if(history.size() == history_size)
            history.pop_front();
        history.push_back(value);
    }

    void clearHistory()
    {
        for(auto &plot : plots)
            for(auto &channel : plot.channels)
                channel.values.clear();
        sample_count = 0;
        last_timestamp_ms = 0;
    }

    std::array<PlotLayout, 3> computePlotLayout() const
    {
        std::array<PlotLayout, 3> layout;

        const int widget_width = std::max(1, width());
        const int widget_height = std::max(1, height());
        const int outer_margin = 10;
        const int header_height = 34;
        const int plot_gap = 18;
        const int legend_width = std::clamp(widget_width / 5, 180, 240);
        const int plots_top = outer_margin + header_height;
        const int plots_bottom = widget_height - outer_margin;
        const int total_gap = plot_gap * static_cast<int>(plots.size() - 1);
        const int available_height = std::max(180, plots_bottom - plots_top - total_gap);
        const int plot_height = std::max(96, available_height / static_cast<int>(plots.size()));
        const int data_left_px = std::min(widget_width - outer_margin - 40, outer_margin + legend_width + 12);
        const int data_right_px = widget_width - outer_margin;

        for(std::size_t index = 0; index < plots.size(); ++index)
        {
            auto &plot = layout[index];
            const int top_px = plots_top + static_cast<int>(index) * (plot_height + plot_gap);
            const int bottom_px = std::min(widget_height - outer_margin, top_px + plot_height);
            plot.title_rect = QRect(outer_margin + 4, top_px + 4, legend_width - 8, 20);
            for(std::size_t channel_index = 0; channel_index < plot.value_rects.size(); ++channel_index)
                plot.value_rects[channel_index] = QRect(outer_margin + 12,
                                                       top_px + 30 + static_cast<int>(channel_index) * 18,
                                                       legend_width - 16,
                                                       18);

            plot.plot_top = pixelToNdcY(static_cast<float>(top_px), static_cast<float>(widget_height));
            plot.plot_bottom = pixelToNdcY(static_cast<float>(bottom_px), static_cast<float>(widget_height));
            plot.plot_mid = 0.5f * (plot.plot_top + plot.plot_bottom);
            plot.data_left = pixelToNdcX(static_cast<float>(data_left_px), static_cast<float>(widget_width));
            plot.data_right = pixelToNdcX(static_cast<float>(data_right_px), static_cast<float>(widget_width));
        }

        return layout;
    }

    static float pixelToNdcX(float px, float widget_width)
    {
        return -1.0f + 2.0f * px / std::max(1.0f, widget_width);
    }

    static float pixelToNdcY(float py, float widget_height)
    {
        return 1.0f - 2.0f * py / std::max(1.0f, widget_height);
    }

    void rebuildVertices()
    {
        vertices.clear();
        grid_vertex_count = 0;
        vertices.reserve(plots.size() * (12 + history_size * 3));

        const auto layout = computePlotLayout();

        for(std::size_t plot_index = 0; plot_index < plots.size(); ++plot_index)
        {
            const auto &plot_layout = layout[plot_index];

            pushLine(plot_layout.data_left, plot_layout.plot_bottom, plot_layout.data_right, plot_layout.plot_bottom, QColor(68, 72, 78));
            pushLine(plot_layout.data_left, plot_layout.plot_top, plot_layout.data_right, plot_layout.plot_top, QColor(68, 72, 78));
            pushLine(plot_layout.data_left, plot_layout.plot_mid, plot_layout.data_right, plot_layout.plot_mid, QColor(90, 95, 100));
            pushLine(plot_layout.data_left, plot_layout.plot_bottom, plot_layout.data_left, plot_layout.plot_top, QColor(68, 72, 78));
            pushLine(plot_layout.data_right, plot_layout.plot_bottom, plot_layout.data_right, plot_layout.plot_top, QColor(68, 72, 78));
        }
        grid_vertex_count = vertices.size();

        for(std::size_t plot_index = 0; plot_index < plots.size(); ++plot_index)
        {
            const auto &plot = plots[plot_index];
            const auto &plot_layout = layout[plot_index];
            const float amplitude = std::max(1e-3f, computeAmplitude(plot));
            const float plot_height = plot_layout.plot_top - plot_layout.plot_bottom;
            const float scale = (plot_height * 0.42f) / amplitude;
            const float x_pad = std::max(0.006f, (plot_layout.data_right - plot_layout.data_left) * 0.02f);
            const float y_pad = std::max(0.008f, plot_height * 0.05f);

            for(const auto &channel : plot.channels)
            {
                const auto count = channel.values.size();
                if(count < 2)
                    continue;

                for(std::size_t i = 0; i < count; ++i)
                {
                    const float x = (plot_layout.data_left + x_pad)
                                  + (plot_layout.data_right - plot_layout.data_left - 2.0f * x_pad)
                                        * static_cast<float>(i) / static_cast<float>(history_size - 1);
                    const float y = std::clamp(plot_layout.plot_mid + channel.values[i] * scale,
                                               plot_layout.plot_bottom + y_pad,
                                               plot_layout.plot_top - y_pad);
                    vertices.push_back(Vertex{x, y, channel.color.redF(), channel.color.greenF(), channel.color.blueF()});
                }
            }
        }
    }

    float computeAmplitude(const Plot &plot) const
    {
        float amplitude = 0.1f;
        for(const auto &channel : plot.channels)
            for(const float value : channel.values)
                amplitude = std::max(amplitude, std::abs(value));
        return amplitude;
    }

    void pushLine(float x0, float y0, float x1, float y1, const QColor &color)
    {
        vertices.push_back(Vertex{x0, y0, color.redF(), color.greenF(), color.blueF()});
        vertices.push_back(Vertex{x1, y1, color.redF(), color.greenF(), color.blueF()});
    }

    void updateGpuBuffers()
    {
        if(!context() || !vbo.isCreated())
            return;

        makeCurrent();
        vbo.bind();
        vbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(Vertex)));
        vbo.release();
        doneCurrent();
    }
};

#endif