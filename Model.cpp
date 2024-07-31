#include "Model.h"
#include <fstream>
#include <sstream>
#include <cassert>

ModelData LoadObjFile(const std::string& _directoryPath, const std::string& _filename)
{
    
    // 1 Decleare variable
    ModelData modelData;
    std::vector<Vector4>    positions;
    std::vector<Vector3>    normals;
    std::vector<Vector2>    texcoords;
    std::string             line;
    // 2 Open file
    std::ifstream file(_directoryPath + "/" + _filename);
    assert(file.is_open());
    // 3 Read file and construct ModelData
    while (std::getline(file, line))
    {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "v")
        {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.w = 1.0f;
            positions.push_back(position);
        }
        else if (identifier == "vt")
        {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoords.push_back(texcoord);
        }
        else if (identifier == "vn")
        {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        }
        else if (identifier == "f")
        {
            // 面は三角形限定。その他は未対応
            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
            {
                std::string vertexDefinition;
                s >> vertexDefinition;
                // 頂点の要素へのIndexは「位置 / UV / 法線」で格納される
                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3];
                for (int32_t element = 0; element < 3; ++element)
                {
                    std::string index;
                    std::getline(v, index, '/'); // [/]区切りでインデックスを読む
                    elementIndices[element] = std::stoi(index);
                }
                // 要素へのIndexから、実際の要素の値を取得して、頂点を構築する
                Vector4 position = positions[elementIndices[0] - 1];
                Vector2 texcoord = texcoords[elementIndices[1] - 1];
                Vector3 normal = normals[elementIndices[2] - 1];
                VertexData vertex = { position, texcoord, normal };
                modelData.vertices.push_back(vertex);
            }
        }
    }
    // 4 Return ModelData
    return modelData;
}
