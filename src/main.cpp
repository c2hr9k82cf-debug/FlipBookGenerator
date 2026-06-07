#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

int main() {
    const std::string inputDir = "assets";
    const std::string outputDir = "output";
    const std::string outputFile = outputDir + "/spritesheet.png";

    // Check input directory
    if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        std::cerr << "Error: " << inputDir << " directory not found." << std::endl;
        return 1;
    }

    // Collect PNG files
    std::vector<std::string> pngFiles;
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (entry.path().extension() == ".png") {
            pngFiles.push_back(entry.path().string());
        }
    }

    if (pngFiles.empty()) {
        std::cerr << "Error: No PNG files found in " << inputDir << std::endl;
        return 1;
    }

    // Sort by filename
    std::sort(pngFiles.begin(), pngFiles.end());

    std::cout << "Found " << pngFiles.size() << " PNG files." << std::endl;

    // Load images
    std::vector<cv::Mat> images;
    for (const auto& file : pngFiles) {
        cv::Mat img = cv::imread(file, cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::cerr << "Warning: Failed to load " << file << ", skipping." << std::endl;
            continue;
        }
        std::cout << "  Loaded: " << file
                  << " (" << img.cols << "x" << img.rows << ")" << std::endl;
        images.push_back(img);
    }

    if (images.empty()) {
        std::cerr << "Error: No images loaded successfully." << std::endl;
        return 1;
    }

    // Concatenate horizontally
    cv::Mat spritesheet;
    cv::hconcat(images, spritesheet);

    // Create output directory and save
    fs::create_directories(outputDir);
    cv::imwrite(outputFile, spritesheet);

    std::cout << "Spritesheet saved: " << outputFile
              << " (" << spritesheet.cols << "x" << spritesheet.rows << ")" << std::endl;

    return 0;
}
