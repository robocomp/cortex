#ifndef CAMERA_API
#define CAMERA_API

#include <dsr/core/topics/IDLGraphPubSubTypes.hpp>
#include <dsr/core/types/user_types.h>
#include <Eigen/Dense>
#include <optional>

namespace DSR
{
    class DSRGraph;

    /**
     * @brief High-level API to access and project camera data stored in the DSR graph.
     *
     * CameraAPI wraps a camera node in the distributed scene graph and exposes
     * convenience methods for:
     * - Retrieving RGB and depth buffers from graph attributes.
     * - Building point clouds from depth data, optionally transformed to a target frame.
     * - Computing ROI-based depth statistics.
     * - Performing geometric conversions between image coordinates, camera rays,
     *   and polar/metric 3D representations.
     *
     * The instance caches camera intrinsics (focal lengths, image center, size,
     * depth/format information) derived from the bound camera node. Stream data
     * access methods fetch fresh node data when needed so callers can work with
     * up-to-date graph values.
     *
     * Typical usage:
     * 1. Construct with a valid camera node from DSRGraph.
     * 2. Query images or point cloud data.
     * 3. Use projection/ray helpers for perception and geometry pipelines.
     * 4. Call reload_camera when camera node parameters change.
     */
    class CameraAPI
    {
        public:
            /// Constructs the API from a graph handle and a camera node.
            explicit CameraAPI(DSRGraph *G_, const DSR::Node &n);
            //explicit CameraAPI(DSRGraph *G_, const std::uint32_t id);
            //explicit CameraAPI(DSRGraph *G_, const std::string &name);

            /// methods that get a fresh copy of the camera node
            //std::optional<std::reference_wrapper<const std::vector<uint8_t>>> get_rgb_image() const;
            /// Returns the RGB image buffer copied from the camera node, if available.
            std::optional<std::vector<uint8_t>> get_rgb_image() ;
            /// Returns the depth image buffer copied from the camera node, if available.
            std::optional<std::vector<float>> get_depth_image(); //returns a copy
            //std::optional<std::reference_wrapper<const std::vector<uint8_t>>> get_depth_image() const;
            /// Builds a point cloud from depth and optionally transforms it to a target frame.
            std::optional<std::vector<std::tuple<float,float,float>>>  get_pointcloud(const std::string& target_frame_node = "", unsigned short subsampling=1);
            /// Converts current depth data into an 8-bit grayscale image representation.
            std::optional<std::vector<uint8_t>> get_depth_as_gray_image() const;

            /// methods that DO NOT ask for a copy of the camera node
            /// Computes representative depth information for a rectangular ROI.
            std::optional<std::tuple<float,float,float>> get_roi_depth(const std::vector<float> &depth, const Eigen::AlignedBox<float, 2> &roi);

            /// Reloads internal camera parameters from the provided camera node.
            bool reload_camera(const DSR::Node &n);
            /// Returns the graph id of the currently bound camera node.
            inline std::uint64_t  get_id() const { return id;};
            /// Converts a 3D point to camera angular coordinates.
            Eigen::Vector3d get_angles( const Eigen::Vector3d & p) const;
            /// Converts a homogeneous 3D point to camera angular coordinates.
            Eigen::Vector3d get_angles_homogeneous( const Eigen::Vector3d & p) const;
            /// Returns a focal value (compatibility helper when a single focal is used).
            float get_focal() const;
            /// Returns horizontal focal length in pixels.
            float get_focal_x() const { return focal_x;};
            /// Returns vertical focal length in pixels.
            float get_focal_y() const { return focal_y;};
            /// Returns image height in pixels.
            inline std::uint32_t get_height() const {return height;};
            /// Returns total image payload size in bytes for current format.
            int get_size_in_bytes() const;
            /// Returns per-pixel depth/channel information for current format.
            int get_depth() const;
            /// Computes the camera ray for a homogeneous image/sample coordinate.
            Eigen::Vector3d get_ray_homogeneous( const Eigen::Vector3d & p) const;
            /// Computes the camera ray for an image/sample coordinate.
            Eigen::Vector3d get_ray(const Eigen::Vector3d & p) const;
            /// Returns image width in pixels.
            inline std::uint32_t get_width() const {return width;};
            /// Converts a polar 3D basis/representation into camera coordinates.
            Eigen::Matrix3d polar_3D_to_camera(const Eigen::Matrix3d& p) const ;
            /// Projects a 3D point into image coordinates using camera intrinsics.
            Eigen::Vector2d project( const Eigen::Vector3d & p, int cx=-1, int cy=-1) const;
            /// Converts a list of RGBD samples (u, v, depth) into camera-frame XYZ points.
            std::vector<Eigen::Vector3d> get_xyz_from_rgbd_points(const std::vector<Eigen::Vector3d> &rgbd_points) const;
            /// Sets a shared focal value for camera intrinsics.
            void set_focal( float f);
            /// Sets horizontal focal length in pixels.
            void set_focal_x( float fx);
            /// Sets vertical focal length in pixels.
            void set_focal_y( float fy);
            /// Sets image width in pixels.
            void set_width( std::uint32_t w);
            /// Sets image height in pixels.
            void set_height( std::uint32_t h);
            /// Translates a point to the camera-centered coordinate frame.
            Eigen::Vector3d to_zero_center( const Eigen::Vector3d &p) const;
            /// Translates a homogeneous point to the camera-centered coordinate frame.
            Eigen::Vector3d to_cero_center_homogeneous( const Eigen::Vector3d &p) const;

        private:
            DSR::Node node;
            DSR::DSRGraph *G;
            std::uint64_t id;
            float focal_x;		        //!< Horizontal focus
            float focal_y;		        //!< Vertical focus
            float focal;
            float centre_x;		        //!< Horizontal position of imagen center in pixel coordinates.
            float centre_y;		        //!< Vertical position of imagen center in pixel coordinates.
            std::uint32_t width;		//!<
            std::uint32_t height;		//!<
            std::uint32_t depth;		//!<
            std::uint32_t cameraID;
    };
}

#endif
