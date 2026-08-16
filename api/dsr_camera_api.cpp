
#include <dsr/api/dsr_camera_api.h>
#include <dsr/api/dsr_api.h>

#include <cmath>

using namespace DSR;

CameraAPI::CameraAPI(DSR::DSRGraph *G_, const DSR::Node &camera)
{
    G = G_;
    id = camera.id();

    // Image size is required for BOTH projection models (equirectangular maps azimuth/elevation onto
    // the full width/height); read it first so the model branch below can rely on it.
    if( auto o_width = G->get_attrib_by_name<cam_rgb_width_att>(camera); o_width.has_value())
    {
        width = o_width.value();
        centre_x = width / 2;
    }
    else qFatal("CameraAPI constructor: aborting since no width attr found in camera");
    if( auto o_height = G->get_attrib_by_name<cam_rgb_height_att>(camera); o_height.has_value())
    {
        height = o_height.value();
        centre_y = height/2;
    }
    else qFatal("CameraAPI constructor: aborting since no height attr found in camera");

    // Pick the projection model from the field of view: a ~2π fov is a 360 panorama (e.g. the ricoh,
    // cam_fov=6.28). Everything else is a perspective pinhole camera. A 360 panorama defaults to spherical
    // Equirectangular, but the node may declare cam_projection="cylindrical" (the webots camera projection):
    // azimuth is linear either way, but the ELEVATION map differs (angle vs tan), so honour it.
    projection_model = ProjectionModel::Pinhole;
    if( auto o_fov = G->get_attrib_by_name<cam_fov_att>(camera); o_fov.has_value() and o_fov.value() >= 5.5f)
    {
        projection_model = ProjectionModel::Equirectangular;
        if( auto o_proj = G->get_attrib_by_name<cam_projection_att>(camera);
            o_proj.has_value() and o_proj.value().get() == "cylindrical")
            projection_model = ProjectionModel::Cylindrical;
    }

    if( projection_model == ProjectionModel::Equirectangular or projection_model == ProjectionModel::Cylindrical )
    {
        // Both 360 models share the panorama COLUMN convention (mirror + seam zero) and carry no focal
        // length. Defaults = textbook (sign +1, offset 0). The row/elevation map differs (see project()).
        if( auto o_sign = G->get_attrib_by_name<cam_equirect_azimuth_sign_att>(camera); o_sign.has_value())
            azimuth_sign = o_sign.value();
        if( auto o_off = G->get_attrib_by_name<cam_equirect_azimuth_offset_att>(camera); o_off.has_value())
            azimuth_offset = o_off.value();
        focal_x = focal_y = focal = 0.f;
    }
    else
    {
        // Pinhole: focal length is mandatory.
        if( auto o_focal_x = G->get_attrib_by_name<cam_rgb_focalx_att>(camera); o_focal_x.has_value())
            focal_x = o_focal_x.value();
        else qFatal("CameraAPI constructor: aborting since no focal_x attr found in perspective camera");
        if( auto o_focal_y = G->get_attrib_by_name<cam_rgb_focaly_att>(camera); o_focal_y.has_value())
            focal_y = o_focal_y.value();
        else qFatal("CameraAPI constructor: aborting since no focal_y attr found in perspective camera");
    }

    if( auto o_depth = G->get_attrib_by_name<cam_rgb_depth_att>(camera); o_depth.has_value())
        depth = o_depth.value();
    else qFatal("CameraAPI constructor: aborting since no depth attr found in camera");
    if( auto o_id = G->get_attrib_by_name<cam_rgb_cameraID_att>(camera); o_id.has_value())
        cameraID = o_id.value();
}

void CameraAPI::set_focal( float f)
{
  focal = f;  focal_x = f; focal_y = f;
}

void CameraAPI::set_focal_x( float fx)
{
    focal_x = fx;
}

void CameraAPI::set_focal_y( float fy)
{
    focal_y = fy;
}

void CameraAPI::set_width( std::uint32_t w)
{
    width = w;
    centre_x = w/2;
}

void CameraAPI::set_height( std::uint32_t h)
{
    height = h;
    centre_y = h/2;
}

// takes a point in camera's 3D coordinates:  Y pointing outwards  and Z upwards
Eigen::Vector2d CameraAPI::project(const Eigen::Vector3d & p, int cx, int  cy) const
{
    Eigen::Vector2d proj;

    if( projection_model == ProjectionModel::Equirectangular )
    {
        // Spherical (360) projection onto an equirectangular panorama. Azimuth θ=atan2(x,y) → column
        // (0 = seam, W/2 = straight ahead), elevation φ=asin(-z/r) → row (top = up). azimuth_sign/offset
        // encode the panorama's column convention (mirror + seam zero). Column is wrapped to [0,W).
        const double r = p.norm();
        if( r < 1e-9 ) { proj << centre_x, centre_y; return proj; }
        double u = ((azimuth_sign * std::atan2(p.x(), p.y()) + azimuth_offset) / (2.0 * M_PI) + 0.5) * width;
        u = std::fmod(u, static_cast<double>(width));
        if( u < 0.0 ) u += width;
        const double v = (std::asin(-p.z() / r) / M_PI + 0.5) * height;
        proj << u, v;
        return proj;
    }

    if( projection_model == ProjectionModel::Cylindrical )
    {
        // Webots "cylindrical" 360 camera: azimuth θ=atan2(x,y) → column (LINEAR, identical to equirect), but
        // the ELEVATION is a PLANAR projection onto the cylinder — row ∝ tan(elevation), NOT the angle itself.
        // Vertical focal f_v = width/(2π) (square pixels; the full 360° spans `width` columns), so the vertical
        // FoV is LIMITED (≈±58° for a 2:1 panorama) — which is exactly why the spherical asin map mis-placed
        // off-horizon (floor) returns onto the wrong rows. v = H/2 − f_v·(z/√(x²+y²)); straight up/down clamps.
        const double rho_h = std::hypot(p.x(), p.y());
        double u = ((azimuth_sign * std::atan2(p.x(), p.y()) + azimuth_offset) / (2.0 * M_PI) + 0.5) * width;
        u = std::fmod(u, static_cast<double>(width));
        if( u < 0.0 ) u += width;
        const double fv = static_cast<double>(width) / (2.0 * M_PI);
        const double v = (rho_h > 1e-9) ? (0.5 * height - fv * (p.z() / rho_h))
                                        : (p.z() >= 0.0 ? 0.0 : static_cast<double>(height));
        proj << u, v;
        return proj;
    }

    if(cx==-1) cx=centre_x;
    if(cy==-1) cy=centre_y;
    proj << focal_x * p.x() / p.y() + cx, -focal_y * p.z() / p.y() + cy;  // Y grows dowwards in the image plane
    //proj << focal_x * /*(608/640) */ p.x() / p.y() + cx, -focal_y * (416./480) * p.z() / p.y() + cy;  //FIXXXXX IT

    return proj;
}

// Inverse of project(): image pixel (u,v) → unit ray in the camera frame (Y forward, Z up).
Eigen::Vector3d CameraAPI::ray_from_pixel(double u, double v) const
{
    if( projection_model == ProjectionModel::Equirectangular )
    {
        // Undo the panorama column/row mapping, then the sign/offset applied in project():
        //   project:  az = sign·atan2(x,y) + offset ;  u = (az/2π + 0.5)·W ;  v = (asin(-z/r)/π + 0.5)·H
        const double az    = (u / static_cast<double>(width)  - 0.5) * 2.0 * M_PI;
        const double phi   = (v / static_cast<double>(height) - 0.5) * M_PI;
        const double theta = azimuth_sign * (az - azimuth_offset);   // atan2(x,y); sign ∈ {+1,-1} ⇒ 1/sign == sign
        const double cphi  = std::cos(phi);
        return { cphi * std::sin(theta), cphi * std::cos(theta), -std::sin(phi) };
    }
    if( projection_model == ProjectionModel::Cylindrical )
    {
        // Inverse of the cylindrical map: azimuth is linear (as equirect), elevation is planar (tan).
        //   project:  u = (az/2π + 0.5)·W ;  v = H/2 − f_v·(z/√(x²+y²)) ,  f_v = W/2π
        const double az    = (u / static_cast<double>(width) - 0.5) * 2.0 * M_PI;
        const double theta = azimuth_sign * (az - azimuth_offset);
        const double fv    = static_cast<double>(width) / (2.0 * M_PI);
        const double t     = (0.5 * static_cast<double>(height) - v) / fv;   // tan(elevation) = z/√(x²+y²)
        return Eigen::Vector3d(std::sin(theta), std::cos(theta), t).normalized();
    }
    // Pinhole: invert u = fx·x/y + cx, v = -fy·z/y + cy at y=1, then normalize.
    const double x = (u - centre_x) / focal_x;
    const double z = (centre_y - v) / focal_y;
    return Eigen::Vector3d(x, 1.0, z).normalized();
}

Eigen::Vector3d CameraAPI::get_ray(const Eigen::Vector3d & p) const
{
    // DECLARED IN THE HEADER SINCE FOREVER, NEVER DEFINED — any caller failed at LINK time with an
    // undefined reference to CameraAPI::get_ray, which reads as a build-system problem rather than a
    // missing implementation. Both entry points delegate to ray_from_pixel() instead of repeating the
    // maths: that is the only place which dispatches on the projection model (Equirectangular uses
    // asin for elevation, Cylindrical uses tan, Pinhole is a third case), so a second copy would
    // drift and be silently wrong on whichever model its author did not have in mind.
    //
    // `p` is an image/sample coordinate (u, v, ·); the third component is IGNORED. Use
    // get_ray_homogeneous() when it carries a homogeneous scale.
    return ray_from_pixel(p.x(), p.y());
}

Eigen::Vector3d CameraAPI::get_ray_homogeneous( const Eigen::Vector3d & p) const
{
    // Homogeneous image coordinate (u·w, v·w, w) → (u, v). A zero or degenerate w cannot be divided
    // through; treat the input as already affine rather than handing back a NaN ray.
    const double w = p.z();
    if( std::abs(w) < 1e-12 )
        return ray_from_pixel(p.x(), p.y());
    return ray_from_pixel(p.x() / w, p.y() / w);
}

std::vector<Eigen::Vector3d> CameraAPI::get_xyz_from_rgbd_points(const std::vector<Eigen::Vector3d> &rgbd_points) const
{
    std::vector<Eigen::Vector3d> xyz_points;
    xyz_points.reserve(rgbd_points.size());

    for (const auto &point : rgbd_points)
    {
        const double u = point.x();
        const double v = point.y();
        const double depth_value = point.z();

        const double X = (u - static_cast<double>(centre_x)) * depth_value / focal_x;
        const double Y = depth_value;
        const double Z = (static_cast<double>(centre_y) - v) * depth_value / focal_y;
        xyz_points.emplace_back(X, Y, Z);
    }

    return xyz_points;
}

std::optional<std::vector<uint8_t>> CameraAPI::get_rgb_image()
{
    if( const auto n = G->get_node(id); n.has_value())
    {
        auto &attrs = n.value().attrs();
        if (auto value = attrs.find("cam_rgb"); value != attrs.end())
            return value->second.byte_vec();
        else
        {
            qWarning() << __FUNCTION__ << "No rgb attribute found in node " << QString::fromStdString(n.value().name()) << ". Returning empty";
            return {};
        }
    }
    else
    {
        qWarning() << __FUNCTION__ << "No camera node found in G. Returning empty";
        return {};
    }
}

std::optional<std::vector<float>> CameraAPI::get_depth_image()
{
    if( const auto n = G->get_node(id); n.has_value())
    {
        auto &attrs = n.value().attrs();
        if (auto value = attrs.find("cam_depth"); value != attrs.end())
        {
            const std::size_t SIZE = value->second.byte_vec().size() / sizeof(float);
            float *depth_array = (float *) value->second.byte_vec().data();
            std::vector<float> res{depth_array, depth_array + SIZE};
            return res;
        } else
        {
            qWarning() << __FUNCTION__ << "No depth attribute found in node " << QString::fromStdString(n.value().name())
                       << ". Returning empty";
            return {};
        }
    }
    else
    {
        qWarning() << __FUNCTION__ << "No camera node found in G. Returning empty";
        return {};
    }
}


///
/// Computes the point clound [X,Y,X] in the target_frame_node coordinate system. Subsampling: 1,2,3.. means all, one of two, one of three, etc
///
std::optional<std::vector<std::tuple<float,float,float>>>  CameraAPI::get_pointcloud(const std::string& target_frame_node, unsigned short subsampling)
{
    if( const auto n = G->get_node(id); n.has_value())
    {
        auto &attrs = n.value().attrs();
        if (auto value = attrs.find("cam_depth"); value != attrs.end())  //in metres
        {
            if (auto width = attrs.find("cam_depth_width"); width != attrs.end())
            {
                if (auto height = attrs.find("cam_depth_height"); height != attrs.end())
                {
                    if (auto focal = attrs.find("cam_depth_focalx"); focal != attrs.end())
                    {
                        const std::vector<uint8_t> &tmp = value->second.byte_vec();
                        if (subsampling == 0 or subsampling > tmp.size())
                        {
                            qWarning("DSRGraph::get_pointcloud: subsampling parameter < 1 or > than depth size");
                            return {};
                        }
                        // cast to float
                        float *depth_array = (float *) value->second.byte_vec().data();
                        const int WIDTH = width->second.dec();
                        const int HEIGHT = height->second.dec();
                        int FOCAL = focal->second.dec();
                        FOCAL = (int) ((WIDTH / 2) / atan(0.52));  // ÑAPA QUITAR
                        int STEP = subsampling;
                        float /*depth,*/ X, Y, Z;
                        int cols, rows;
                        std::size_t SIZE = tmp.size() / sizeof(float);
                        std::vector<std::tuple<float, float, float>> result(SIZE/STEP);
                        std::unique_ptr<InnerEigenAPI> inner_eigen;
                        if (!target_frame_node.empty())  // do the change of coordinate system
                        {
                            inner_eigen = G->get_inner_eigen_api();
                            for (std::size_t i = 0; i < SIZE/STEP; i += 1)
                            {
                                //depth = depth_array[i];
                                cols = (i % WIDTH) - (WIDTH / 2);
                                rows = (HEIGHT / 2) - (i / WIDTH);
                                // compute axis coordinates according to the camera's coordinate system (Y outwards and Z up)
                                Y = depth_array[i];
                                X = cols * Y / FOCAL;
                                Z = rows * Y / FOCAL;
                                auto r = inner_eigen->transform(target_frame_node, Mat::Vector3d(X, Y, Z),
                                                                n.value().name()).value();
                                result[i] = std::make_tuple(r[0], r[1], r[2]);
                            }
                        } else
                            for (std::size_t i = 0; i < SIZE/STEP; i += 1)
                            {
                                cols = (i % WIDTH) - (WIDTH / 2);
                                rows = (HEIGHT / 2) - (i / WIDTH);
                                // compute axis coordinates according to the camera's coordinate system (Y outwards and Z up)
                                Y = depth_array[i];
                                X = cols * Y / FOCAL;
                                Z = rows * Y / FOCAL;
                                result[i] = std::make_tuple(X, Y, Z);
                            }
                        return result;
                    } else
                    {
                        qWarning() << __FUNCTION__ << "No focal attribute found in node "
                                   << QString::fromStdString(n.value().name()) << ". Returning empty";
                        return {};
                    }
                } else
                {
                    qWarning() << __FUNCTION__ << "No HEIGHT attribute found in node "
                               << QString::fromStdString(n.value().name())
                               << ". Returning empty";
                    return {};
                }
            } else
            {
                qWarning() << __FUNCTION__ << "No WIDTH attribute found in node " << QString::fromStdString(n.value().name())
                           << ". Returning empty";
                return {};
            }
        } else
        {
            qWarning() << __FUNCTION__ << "No depth attribute found found in node " << QString::fromStdString(n.value().name())
                       << ". Returning empty";
            return {};
        }
    }
    else
    {
        qWarning() << __FUNCTION__ << "No camera node found in G. Returning empty";
        return {};
    }
}

std::optional<std::vector<uint8_t>> CameraAPI::get_depth_as_gray_image() const
{
    if( const auto n = G->get_node(id); n.has_value())
    {
        auto &attrs = n.value().attrs();
        if (auto value = attrs.find("cam_depth"); value != attrs.end())
        {
            const std::vector<uint8_t> &tmp = value->second.byte_vec();
            float *depth_array = (float *) value->second.byte_vec().data();
            const auto STEP = sizeof(float);
            std::vector<std::uint8_t> gray_image(tmp.size() / STEP);
            for (std::size_t i = 0; i < tmp.size() / STEP; i++)
                gray_image[i] = (int) (depth_array[i] * 15);  // ONLY VALID FOR SHORT RANGE, INDOOR SCENES
            return gray_image;
        } else
        {
            qWarning() << __FUNCTION__ << "No depth attribute found in node " << QString::fromStdString(n.value().name())
                       << ". Returning empty";
            return {};
        };
    }
    else
    {
        qWarning() << __FUNCTION__ << "No camera node found in G. Returning empty";
        return {};
    }
}

std::optional<std::tuple<float,float,float>> CameraAPI::get_roi_depth(const std::vector<float> &depth, const Eigen::AlignedBox<float, 2> &roi)
{
    auto left = (int)roi.min().x(); auto bot = (int)roi.min().y();
    auto right = (int)roi.max().x(); auto top = (int)roi.max().y();  // botom has higher numeric value. rows start in 0 up
    if(left<right and bot>top)
    {
        
        auto size = (right - left) * (bot - top);
        std::vector<float> values(size);
        std::size_t k = 0;
        for (int i = top; i < bot; i++)
            for (int j = left; j < right; j++)
                values[k++] = depth[i * width + j];

        //auto mv = std::ranges::min(values);
        //std::nth_element(values.begin(), values.begin() + values.size()/2, values.end());
        std::sort(values.begin(), values.end());
        // const int px = (left + right) / 2;
        // const int py = (top + bot) / 2;
        // const auto mv = depth[py * width + px];
        const auto mv = values[values.size()/2];  //median
        const auto Y = mv * 1000;
        const float cols = left + (right - left) / 2;
        const float rows = top + (bot - top) / 2;
        float X = (cols - this->width/2) * Y / this->focal_x;
        float Z = (this->height/2 - rows)  * Y /  this->focal_y;
        return std::make_tuple(X, Y, Z);
    }
    else
    {
        qWarning() << __FUNCTION__ << "Incorrect ROI dimensions l r t b: " << left << right << top << bot << ". Returning empty";
        return {};
    }
}