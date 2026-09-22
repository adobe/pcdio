/*
 * Copyright 2026 Adobe. All rights reserved.
 * This file is licensed to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
 * OF ANY KIND, either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#include <pcdio/pcdio.h>

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: pcd_inspect input.pcd" << std::endl;
        return 1;
    }

    pcdio::PcdSpec spec = pcdio::load_pcd(argv[1]);

    std::cout << "Version: " << spec.version << std::endl;
    std::cout << "Width: " << spec.width << std::endl;
    std::cout << "Height: " << spec.height << std::endl;
    std::cout << "Points: " << spec.points << std::endl;
    std::cout << "Data encoding: " << spec.data << std::endl;
    std::cout << "Viewpoint:";
    for (double v : spec.viewpoint) std::cout << " " << v;
    std::cout << std::endl;

    std::cout << "Num fields: " << spec.fields.size() << std::endl;
    for (const auto& field : spec.fields) {
        std::cout << "  " << field.name << ": type " << field.type << ", size " << field.size
                  << ", count " << field.count << " (" << field.data.size() << " bytes)"
                  << std::endl;
    }

    // Print the first few points if the cloud has float x/y/z fields.
    const pcdio::PcdField* fx = spec.find_field("x");
    const pcdio::PcdField* fy = spec.find_field("y");
    const pcdio::PcdField* fz = spec.find_field("z");
    if (fx != nullptr && fy != nullptr && fz != nullptr && fx->type == 'F' && fx->count == 1 &&
        fy->type == 'F' && fy->count == 1 && fz->type == 'F' && fz->count == 1) {
        const size_t num_preview = spec.points < 5 ? spec.points : 5;
        if (fx->size == 8 && fy->size == 8 && fz->size == 8) {
            const double* xs = fx->get_data<double>();
            const double* ys = fy->get_data<double>();
            const double* zs = fz->get_data<double>();
            for (size_t i = 0; i < num_preview; i++) {
                std::cout << "  point " << i << ": " << xs[i] << " " << ys[i] << " " << zs[i]
                          << std::endl;
            }
        } else if (fx->size == 4 && fy->size == 4 && fz->size == 4) {
            const float* xs = fx->get_data<float>();
            const float* ys = fy->get_data<float>();
            const float* zs = fz->get_data<float>();
            for (size_t i = 0; i < num_preview; i++) {
                std::cout << "  point " << i << ": " << xs[i] << " " << ys[i] << " " << zs[i]
                          << std::endl;
            }
        }
    }

    pcdio::save_pcd("tmp.pcd", spec);
    std::cout << "Saved a copy to tmp.pcd" << std::endl;

    return 0;
}
