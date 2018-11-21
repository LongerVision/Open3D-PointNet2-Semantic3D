#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>

#include <Eigen/Dense>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/common/pca.h>

#include "Core/Core.h"
#include "IO/IO.h"

typedef pcl::PointXYZRGBNormal Point;
typedef pcl::PointCloud<Point> PointCloud;
typedef PointCloud::Ptr PointCloudPtr;

static std::vector<std::string> file_prefixes{
    // "bildstein_station1_xyz_intensity_rgb",
    // "bildstein_station3_xyz_intensity_rgb",
    // "bildstein_station5_xyz_intensity_rgb",
    // "domfountain_station1_xyz_intensity_rgb",
    // "domfountain_station2_xyz_intensity_rgb",
    // "domfountain_station3_xyz_intensity_rgb",
    // "neugasse_station1_xyz_intensity_rgb",
    // "sg27_station1_intensity_rgb",
    // "sg27_station2_intensity_rgb",

    // "sg27_station4_intensity_rgb",
    // "sg27_station5_intensity_rgb",
    // "sg27_station9_intensity_rgb",
    // "sg28_station4_intensity_rgb",
    "untermaederbrunnen_station1_xyz_intensity_rgb",
    // "untermaederbrunnen_station3_xyz_intensity_rgb",

    // "birdfountain_station1_xyz_intensity_rgb",
    // "castleblatten_station1_intensity_rgb",
    // "castleblatten_station5_xyz_intensity_rgb",
    // "marketplacefeldkirch_station1_intensity_rgb",
    // "marketplacefeldkirch_station4_intensity_rgb",
    // "marketplacefeldkirch_station7_intensity_rgb",
    // "sg27_station10_intensity_rgb",
    // "sg27_station3_intensity_rgb",
    // "sg27_station6_intensity_rgb",
    // "sg27_station8_intensity_rgb",
    // "sg28_station2_intensity_rgb",
    // "sg28_station5_xyz_intensity_rgb",
    // "stgallencathedral_station1_intensity_rgb",
    // "stgallencathedral_station3_intensity_rgb",
    // "stgallencathedral_station6_intensity_rgb"
};

// class for voxels
class VoxelCenter {
   public:
    double x, y, z;
    double r, g, b;
    int label;
};

// class for sample of n points in voxel
// We don't stay with n points all the way through :
// if the surface is flat, we drop all but one of them
class SamplePointsContainer {
   public:
    size_t n, i;
    std::vector<VoxelCenter> points_;
    SamplePointsContainer() {
        n = 10;
        i = 0;
        points_ = std::vector<VoxelCenter>(n);
    }
    void insert_if_room(VoxelCenter vc);
    void resize();
    int size() { return points_.size(); }
    std::vector<VoxelCenter>::iterator begin() { return points_.begin(); }
    std::vector<VoxelCenter>::iterator end() { return points_.end(); }
};

// We fill the container with (n=10) at most and we don't accept points that are
// too close together
void SamplePointsContainer::insert_if_room(VoxelCenter vc) {
    if (i < n) {
        double dmin = 1e7;
        for (size_t j = 0; j < i; j++) {
            double d = (vc.x - points_[j].x) * (vc.x - points_[j].x) +
                       (vc.y - points_[j].y) * (vc.y - points_[j].y) +
                       (vc.z - points_[j].z) * (vc.z - points_[j].z);
            if (d < dmin) dmin = d;
        }
        if (dmin > 0.001) {
            points_[i] = vc;
            i++;
        }
    }
    if (i == n) {
        resize();  // resizing in order to reduce memory allocation
        i++;  // in order to go quickly though this function all the next times
    }
}

// Flatness is calculated with pca on the points in the container.
void SamplePointsContainer::resize() {
    if (i < 3) {
        points_.resize(i);
        return;
    }
    PointCloudPtr smallpc(new PointCloud);
    smallpc->width = n;
    smallpc->height = 1;
    smallpc->is_dense = false;
    smallpc->points.resize(smallpc->width * smallpc->height);

    // Fix deprecation warning
    // pcl::PCA<Point> pca(*smallpc);
    pcl::PCA<Point> pca;
    pca.setInputCloud(smallpc);

    Eigen::Vector3f eigenvalues = pca.getEigenValues();
    if (eigenvalues(2) > 0.00001)
        points_.resize(4);
    else
        points_.resize(1);
}

// comparator for voxels
struct Vector3iComp {
    bool operator()(const Eigen::Vector3i& v1,
                    const Eigen::Vector3i& v2) const {
        if (v1[0] < v2[0]) {
            return true;
        } else if (v1[0] == v2[0]) {
            if (v1[1] < v2[1]) {
                return true;
            } else if (v1[1] == v2[1] && v1[2] < v2[2]) {
                return true;
            }
        }
        return false;
    }
};

Eigen::Vector3i get_voxel(double x, double y, double z, double voxel_size) {
    int x_index = std::floor(x / voxel_size) + 0.5;
    int y_index = std::floor(y / voxel_size) + 0.5;
    int z_index = std::floor(z / voxel_size) + 0.5;
    return Eigen::Vector3i(x_index, y_index, z_index);
}

Eigen::Vector3i get_voxel(const Eigen::Vector3d& point, double voxel_size) {
    return get_voxel(point(0), point(1), point(2), voxel_size);
}

std::vector<int> read_labels(const std::string& file_path) {
    std::vector<int> labels;
    std::ifstream infile(file_path);
    int label;
    if (infile.fail()) {
        throw std::runtime_error(file_path + " not found at read_labels");
    } else {
        while (infile >> label) {
            labels.push_back(label);
        }
    }
    infile.close();
    return labels;
}

void write_labels(const std::vector<int>& labels,
                  const std::string& file_path) {
    std::cout << "Writting dense labels" << std::endl;
    // Using C fprintf is much faster than C++ streams
    FILE* f = fopen(file_path.c_str(), "w");
    if (f == nullptr) {
        throw std::runtime_error("Output file cannot be created: " + file_path +
                                 " Consider creating the directory first");
    }
    for (const int& label : labels) {
        fprintf(f, "%d\n", label);
    }
    fclose(f);
    std::cout << "Output written to: " << file_path << std::endl;
}

void adaptive_sampling(const std::string& dense_dir,
                       const std::string& sparse_dir,
                       const std::string& file_prefix, double voxel_size) {
    std::cout << "[Down-sampling] " << file_prefix << std::endl;

    // Paths
    std::string dense_points_path = dense_dir + "/" + file_prefix + ".pcd";
    std::string dense_labels_path = dense_dir + "/" + file_prefix + ".labels";
    std::string sparse_points_path =
        sparse_dir + "/" + file_prefix + "_all.txt";

    // Read dense points
    open3d::PointCloud dense_pcd;
    open3d::ReadPointCloud(dense_points_path, dense_pcd);
    std::cout << dense_pcd.points_.size() << " dense points" << std::endl;

    // Read dense labels
    std::vector<int> dense_labels;
    bool has_label = true;
    try {
        dense_labels = read_labels(dense_labels_path);
        std::cout << dense_labels.size() << " dense labels" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Dense labels not found, treating as tests" << std::endl;
        has_label = false;
    }

    std::map<Eigen::Vector3i, SamplePointsContainer, Vector3iComp> voxels;
    std::cout << "dense_labels.size() " << dense_labels.size() << std::endl;
    for (size_t dense_idx = 0; dense_idx < dense_labels.size(); dense_idx++) {
        // Get label
        int dense_label = 0;
        if (has_label) {
            dense_label = dense_labels[dense_idx];
            // Skip the points with label 0
            if (dense_label == 0) {
                continue;
            }
        }

        // TODO: remove this, Get point
        double x = dense_pcd.points_[dense_idx][0];
        double y = dense_pcd.points_[dense_idx][1];
        double z = dense_pcd.points_[dense_idx][2];
        double r = dense_pcd.colors_[dense_idx][0];
        double g = dense_pcd.colors_[dense_idx][1];
        double b = dense_pcd.colors_[dense_idx][2];

        // Get voxel
        Eigen::Vector3i vox = get_voxel(x, y, z, voxel_size);

        // Build map
        if (voxels.count(vox) > 0) {
            VoxelCenter vc;
            vc.x = std::floor(x / voxel_size) * voxel_size;
            vc.y = std::floor(y / voxel_size) * voxel_size;
            vc.z = std::floor(z / voxel_size) * voxel_size;
            vc.r = r;
            vc.g = g;
            vc.b = b;
            vc.label = dense_label;
            voxels[vox].insert_if_room(vc);

        } else {
            SamplePointsContainer container;
            VoxelCenter vc;
            vc.x = std::floor(x / voxel_size) * voxel_size;
            vc.y = std::floor(y / voxel_size) * voxel_size;
            vc.z = std::floor(z / voxel_size) * voxel_size;
            vc.r = r;
            vc.g = g;
            vc.b = b;
            vc.label = dense_label;
            container.insert_if_room(vc);
            voxels[vox] = container;
        }

        if (dense_idx % 1000000 == 0) {
            std::cout << dense_idx << " processed" << std::endl;
        }
    }

    // Resizing point containers
    for (auto it = voxels.begin(); it != voxels.end(); it++) {
        it->second.resize();
    }
    std::cout << "Exporting result of decimation" << std::endl;

    std::ofstream output(sparse_points_path.c_str());
    for (auto it = voxels.begin(); it != voxels.end(); it++) {
        SamplePointsContainer spc = it->second;
        for (auto it2 = spc.begin(); it2 != spc.end(); it2++) {
            output << it2->x << " " << it2->y << " " << it2->z << " "  //
                   << it2->r << " " << it2->g << " " << it2->b;
            if (has_label) {
                output << " " << it2->label;
            }
            output << std::endl;
        }
    }
}

int main(int argc, char** argv) {
    // Parse arguments
    if (argc < 4) {
        std::cerr << "USAGE : " << argv[0] << " dense_dir sparse_dir voxel_size"
                  << std::endl;
        exit(1);
    }
    double voxel_size = strtof(argv[3], NULL);
    std::string input_dir = argv[1];
    std::string output_dir = argv[2];

    // Down sample
    for (const std::string& file_prefix : file_prefixes) {
        std::cout << "adaptive sampling for " + file_prefix << std::endl;
        adaptive_sampling(input_dir, output_dir, file_prefix, voxel_size);
    }
}
