#pragma once
#include<vector>
#include<string>
#include<memory>
#include<filesystem>
#include "Triangle.h"
#include "Material.h"
#include "Hittable_List.h"

#include "external/tiny_obj_loader.h"

class ObjLoader {
public:
    static std::shared_ptr<Hittable_List> load(
        const std::filesystem::path& filepath, std::shared_ptr<Material> default_mat)
    {
        auto mesh_list = std::make_shared<Hittable_List>();

        tinyobj::ObjReaderConfig reader_config;
        // If there has a mtl file
        reader_config.mtl_search_path = "./";

        tinyobj::ObjReader reader;
        if (!reader.ParseFromFile(filepath.string(), reader_config))
        {
            if (!reader.Error().empty())
            {
                std::cerr << "TinyObjReader Error: " << reader.Error();
            }
            return nullptr;
        }

        if (!reader.Warning().empty())
        {
            std::cout << "TinyObjReader Warning: " << reader.Warning();
        }

        auto& attrib = reader.GetAttrib();   // Vertex, Normal, UV
        auto& shapes = reader.GetShapes();   // Face

        for (size_t s = 0; s < shapes.size(); s++)
        {
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
            {
                size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

                if (fv == 3)
                {
                    Point3 vertices[3];
                    for (size_t v = 0; v < 3; v++)
                    {
                        tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                        // Abstract Vectex's Coordinate
                        tinyobj::real_t vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
                        tinyobj::real_t vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
                        tinyobj::real_t vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

                        vertices[v] = Point3(vx, vy, vz);

                        // TODO : Abstract Normal
                    }

                    mesh_list->add(std::make_shared<Triangle>(vertices[0], vertices[1], vertices[2], default_mat));
                }
                index_offset += fv;
            }
        }

        return mesh_list;
    }
};