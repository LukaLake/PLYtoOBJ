// PLYtoOBJ.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm> // for std::reverse, std::find
#include <cstdint>   // for uint32_t, int16_t etc.
#include <type_traits> // for std::is_arithmetic
#include <chrono>      // 用于计时

using namespace std;

// --- (Vec2, Vec3, Vertex, Triangle 结构定义保持不变) ---
// 自定义二维向量结构 (用于纹理坐标)
struct Vec2 {
    float u, v;
    Vec2(float u = 0.0f, float v = 0.0f) : u(u), v(v) {}
};

// 自定义三维向量结构
struct Vec3 {
    float x, y, z;
    Vec3(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}
};

// 扩展顶点数据结构
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color; // 存储为 0.0f - 1.0f 的浮点数
    Vec2 texCoord;

    bool has_normal = false;
    bool has_color = false;
    bool has_texCoord = false;
};

// 三角形面结构（存储三个顶点索引）
struct Triangle {
    int v0, v1, v2;
};


// 辅助结构，用于存储PLY属性信息
struct PlyProperty {
    string name;
    string type_str; // PLY中的数据类型字符串，如 "float", "uchar"
    // 对于列表属性 (如面索引)
    bool is_list = false;
    string count_type_str; // 列表的计数的类型 (e.g., uchar, ushort)
    string list_item_type_str; // 列表项的类型 (e.g., int)

    // 用于ASCII解析
    int index_in_line = -1;
};

// 辅助函数：检查系统字节序
bool isSystemLittleEndian() {
    uint32_t i = 1;
    char* c = (char*)&i;
    return (*c == 1);
}

// 辅助函数：字节交换
template<typename T>
T swapBytes(T value) {
    static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value, "swapBytes can only be used with arithmetic or enum types");
    char* bytes = reinterpret_cast<char*>(&value);
    std::reverse(bytes, bytes + sizeof(T));
    return value;
}

// 辅助模板函数：从文件读取二进制值并处理字节序
template<typename T>
bool readBinary(std::ifstream& file, T& value, bool fileIsLittleEndian, bool systemIsLittleEndian) {
    if (!file.read(reinterpret_cast<char*>(&value), sizeof(T))) {
        return false;
    }
    if (fileIsLittleEndian != systemIsLittleEndian) {
        value = swapBytes(value);
    }
    return true;
}

// 特化版本，用于读取PLY颜色 (uchar) 并转换为 float (0-1)
bool readBinaryColorComponent(std::ifstream& file, float& color_component, bool fileIsLittleEndian, bool systemIsLittleEndian) {
    unsigned char uchar_val;
    if (!readBinary(file, uchar_val, fileIsLittleEndian, systemIsLittleEndian)) { // uchar 不需要字节交换，但保持接口一致
        return false;
    }
    color_component = static_cast<float>(uchar_val) / 255.0f;
    return true;
}


// 读取PLY文件并提取顶点和三角形数据
bool readPLY(const string& plyPath, vector<Vertex>& vertices_out, vector<Triangle>& triangles_out,
    bool& file_has_normals, bool& file_has_colors, bool& file_has_texCoords) {
    ifstream file(plyPath, ios::in | ios::binary);
    if (!file.is_open()) {
        cerr << "错误: 无法打开PLY文件 " << plyPath << endl;
        return false;
    }

    string line;
    long vertexCount = 0, faceCount = 0;
    bool headerEnd = false;
    bool isASCII = true; // 默认为ASCII
    bool plyFileIsLittleEndian = false; // PLY文件的字节序

    vector<PlyProperty> vertexProperties;
    PlyProperty faceProperty; // 假设只有一个面属性 "vertex_indices" 或 "vertex_index"
    bool facePropertyDefined = false;

    int currentPropertyIndexASCII = 0;

    file_has_normals = false;
    file_has_colors = false;
    file_has_texCoords = false;

    string currentElement = "";

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.rfind("comment", 0) == 0) continue;

        istringstream iss(line);
        string token;
        iss >> token;

        if (token == "ply") continue;
        if (token == "format") {
            string format_str, version_str;
            iss >> format_str >> version_str;
            if (format_str == "ascii") {
                isASCII = true;
            }
            else if (format_str == "binary_little_endian") {
                isASCII = false;
                plyFileIsLittleEndian = true;
            }
            else if (format_str == "binary_big_endian") {
                isASCII = false;
                plyFileIsLittleEndian = false;
            }
            else {
                cerr << "错误: 不支持的PLY格式: " << format_str << endl;
                return false;
            }
        }
        else if (token == "element") {
            iss >> currentElement;
            if (currentElement == "vertex") iss >> vertexCount;
            else if (currentElement == "face") iss >> faceCount;
            currentPropertyIndexASCII = 0;
        }
        else if (token == "property") {
            PlyProperty prop;
            string type_or_list;
            iss >> type_or_list;

            if (type_or_list == "list") {
                prop.is_list = true;
                iss >> prop.count_type_str >> prop.list_item_type_str >> prop.name;
            }
            else {
                prop.is_list = false;
                prop.type_str = type_or_list;
                iss >> prop.name;
            }

            if (currentElement == "vertex") {
                prop.index_in_line = currentPropertyIndexASCII++;
                vertexProperties.push_back(prop);
                if (prop.name == "nx" || prop.name == "ny" || prop.name == "nz") file_has_normals = true;
                if (prop.name == "red" || prop.name == "green" || prop.name == "blue" || prop.name == "alpha") file_has_colors = true;
                if (prop.name == "u" || prop.name == "v" || prop.name == "s" || prop.name == "t" || prop.name == "texture_u" || prop.name == "texture_v") file_has_texCoords = true;
            }
            else if (currentElement == "face") {
                if (prop.name == "vertex_indices" || prop.name == "vertex_index") {
                    faceProperty = prop;
                    facePropertyDefined = true;
                }
                else {
                    // cerr << "警告: 面元素中存在未处理的属性: " << prop.name << endl;
                }
            }
        }
        else if (token == "end_header") {
            headerEnd = true;
            // 对于二进制文件，头之后的第一件事就是数据，所以我们需要确保文件指针在正确的位置。
            // getline 会消耗换行符。如果这是二进制文件，我们需要确保我们从下一行开始。
            // 通常，二进制数据紧跟在 "end_header\n" 之后。
            // 如果文件是以 ios::binary 打开的，getline 仍然有效，但读取二进制数据时要用 file.read()
            break;
        }
    }

    if (!headerEnd) {
        cerr << "错误: 无效的PLY文件头或未找到end_header" << endl;
        return false;
    }
    if (faceCount > 0 && !facePropertyDefined) {
        cerr << "错误: 定义了面元素但未找到 'vertex_indices' 或 'vertex_index' 属性。" << endl;
        return false;
    }


    bool systemIsLE = isSystemLittleEndian();
    vertices_out.resize(vertexCount);

    // 读取顶点数据
    for (long i = 0; i < vertexCount; ++i) {
        Vertex currentVertex;
        if (isASCII) {
            if (!getline(file, line)) {
                cerr << "错误: 读取ASCII顶点数据时意外结束 (顶点 " << i << "/" << vertexCount << ")" << endl;
                return false;
            }
            if (line.empty() && i < vertexCount - 1) {
                cerr << "错误: 读取ASCII顶点数据时遇到空行 (顶点 " << i << "/" << vertexCount << ")" << endl;
                return false;
            }
            else if (line.empty()) continue;

            istringstream iss(line);
            vector<string> values_str;
            string val_s;
            while (iss >> val_s) {
                values_str.push_back(val_s);
            }

            for (const auto& prop : vertexProperties) {
                if (prop.index_in_line >= values_str.size()) {
                    // cerr << "警告: ASCII顶点 " << i << " 的属性 " << prop.name << " 数据不足。" << endl;
                    continue;
                }
                const string& str_val = values_str[prop.index_in_line];
                float f_val = 0;
                unsigned char uc_val = 0;
                try {
                    if (prop.type_str == "float" || prop.type_str == "float32" || prop.type_str == "double" || prop.type_str == "float64") {
                        f_val = std::stof(str_val);
                    }
                    else if (prop.type_str == "uchar" || prop.type_str == "uint8" || prop.type_str == "char" || prop.type_str == "int8") {
                        uc_val = static_cast<unsigned char>(std::stoi(str_val));
                    }
                    // 可以添加对其他整数类型的支持
                }
                catch (const std::invalid_argument& ia) {
                    cerr << "错误: ASCII顶点 " << i << " 属性 " << prop.name << " 值无效: " << str_val << endl; continue;
                }
                catch (const std::out_of_range& oor) {
                    cerr << "错误: ASCII顶点 " << i << " 属性 " << prop.name << " 值超出范围: " << str_val << endl; continue;
                }


                if (prop.name == "x") currentVertex.position.x = f_val;
                else if (prop.name == "y") currentVertex.position.y = f_val;
                else if (prop.name == "z") currentVertex.position.z = f_val;
                else if (prop.name == "nx") { currentVertex.normal.x = f_val; currentVertex.has_normal = true; }
                else if (prop.name == "ny") { currentVertex.normal.y = f_val; currentVertex.has_normal = true; }
                else if (prop.name == "nz") { currentVertex.normal.z = f_val; currentVertex.has_normal = true; }
                else if (prop.name == "red") { currentVertex.color.x = (prop.type_str == "uchar" || prop.type_str == "uint8") ? uc_val / 255.0f : f_val; currentVertex.has_color = true; }
                else if (prop.name == "green") { currentVertex.color.y = (prop.type_str == "uchar" || prop.type_str == "uint8") ? uc_val / 255.0f : f_val; currentVertex.has_color = true; }
                else if (prop.name == "blue") { currentVertex.color.z = (prop.type_str == "uchar" || prop.type_str == "uint8") ? uc_val / 255.0f : f_val; currentVertex.has_color = true; }
                else if (prop.name == "u" || prop.name == "texture_u" || prop.name == "s") { currentVertex.texCoord.u = f_val; currentVertex.has_texCoord = true; }
                else if (prop.name == "v" || prop.name == "texture_v" || prop.name == "t") { currentVertex.texCoord.v = f_val; currentVertex.has_texCoord = true; }
            }
        }
        else { // 二进制读取
            for (const auto& prop : vertexProperties) {
                if (prop.name == "x") { if (!readBinary(file, currentVertex.position.x, plyFileIsLittleEndian, systemIsLE)) return false; }
                else if (prop.name == "y") { if (!readBinary(file, currentVertex.position.y, plyFileIsLittleEndian, systemIsLE)) return false; }
                else if (prop.name == "z") { if (!readBinary(file, currentVertex.position.z, plyFileIsLittleEndian, systemIsLE)) return false; }
                else if (prop.name == "nx") { if (!readBinary(file, currentVertex.normal.x, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_normal = true; }
                else if (prop.name == "ny") { if (!readBinary(file, currentVertex.normal.y, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_normal = true; }
                else if (prop.name == "nz") { if (!readBinary(file, currentVertex.normal.z, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_normal = true; }
                else if (prop.name == "red") { if (!readBinaryColorComponent(file, currentVertex.color.x, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_color = true; }
                else if (prop.name == "green") { if (!readBinaryColorComponent(file, currentVertex.color.y, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_color = true; }
                else if (prop.name == "blue") { if (!readBinaryColorComponent(file, currentVertex.color.z, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_color = true; }
                else if (prop.name == "alpha") { // 通常alpha是uchar，如果需要读取
                    unsigned char alpha_val;
                    if (!readBinary(file, alpha_val, plyFileIsLittleEndian, systemIsLE)) return false;
                    // currentVertex.alpha = alpha_val / 255.0f; // 如果Vertex结构有alpha
                }
                else if (prop.name == "u" || prop.name == "texture_u" || prop.name == "s") { if (!readBinary(file, currentVertex.texCoord.u, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_texCoord = true; }
                else if (prop.name == "v" || prop.name == "texture_v" || prop.name == "t") { if (!readBinary(file, currentVertex.texCoord.v, plyFileIsLittleEndian, systemIsLE)) return false; currentVertex.has_texCoord = true; }
                else { // 跳过未知或不需要的二进制属性
                    size_t typeSize = 0;
                    if (prop.type_str == "char" || prop.type_str == "int8" || prop.type_str == "uchar" || prop.type_str == "uint8") typeSize = 1;
                    else if (prop.type_str == "short" || prop.type_str == "int16" || prop.type_str == "ushort" || prop.type_str == "uint16") typeSize = 2;
                    else if (prop.type_str == "int" || prop.type_str == "int32" || prop.type_str == "uint" || prop.type_str == "uint32" || prop.type_str == "float" || prop.type_str == "float32") typeSize = 4;
                    else if (prop.type_str == "double" || prop.type_str == "float64") typeSize = 8;

                    if (typeSize > 0) {
                        file.seekg(typeSize, ios_base::cur);
                        if (file.fail()) { cerr << "错误: 跳过二进制顶点属性 " << prop.name << " 时读取失败。" << endl; return false; }
                    }
                    else {
                        cerr << "警告: 无法确定二进制顶点属性 " << prop.name << " (类型: " << prop.type_str << ") 的大小以跳过。" << endl;
                    }
                }
            }
        }
        vertices_out[i] = currentVertex;
    }

    for (const auto& v : vertices_out) {
        if (v.has_normal) file_has_normals = true;
        if (v.has_color) file_has_colors = true;
        if (v.has_texCoord) file_has_texCoords = true;
    }

    // 读取面数据
    triangles_out.reserve(faceCount);
    for (long i = 0; i < faceCount; ++i) {
        unsigned char numFaceVertices_uchar = 0;
        unsigned short numFaceVertices_ushort = 0;
        uint32_t numFaceVertices_uint = 0;
        int numFaceVertices = 0;

        if (isASCII) {
            if (!getline(file, line)) {
                cerr << "错误: 读取ASCII面数据时意外结束 (面 " << i << "/" << faceCount << ")" << endl;
                return false;
            }
            if (line.empty() && i < faceCount - 1) {
                cerr << "错误: 读取ASCII面数据时遇到空行 (面 " << i << "/" << faceCount << ")" << endl;
                return false;
            }
            else if (line.empty()) continue;
            istringstream iss(line);
            iss >> numFaceVertices;
        }
        else { // 二进制
            if (faceProperty.count_type_str == "uchar" || faceProperty.count_type_str == "uint8") {
                if (!readBinary(file, numFaceVertices_uchar, plyFileIsLittleEndian, systemIsLE)) return false;
                numFaceVertices = numFaceVertices_uchar;
            }
            else if (faceProperty.count_type_str == "ushort" || faceProperty.count_type_str == "uint16") {
                if (!readBinary(file, numFaceVertices_ushort, plyFileIsLittleEndian, systemIsLE)) return false;
                numFaceVertices = numFaceVertices_ushort;
            }
            else if (faceProperty.count_type_str == "uint" || faceProperty.count_type_str == "uint32") {
                if (!readBinary(file, numFaceVertices_uint, plyFileIsLittleEndian, systemIsLE)) return false;
                numFaceVertices = numFaceVertices_uint;
            }
            // 添加对其他计数类型的支持，如 short, int
            else {
                cerr << "错误: 不支持的面顶点计数的二进制类型: " << faceProperty.count_type_str << endl;
                return false;
            }
        }

        if (numFaceVertices < 3) {
            // cerr << "警告: 面 " << i << " 的顶点数少于3 (" << numFaceVertices << ")。" << endl;
            // 在二进制中，需要读取并丢弃这些索引以保持文件流同步
            for (int j = 0; j < numFaceVertices; ++j) {
                if (isASCII) {
                    // 对于ASCII，如果整行已读入iss，则不需要额外操作来跳过
                    // 但如果面数据不符合预期，iss的后续读取会失败
                }
                else {
                    // 根据 faceProperty.list_item_type_str 跳过相应字节
                    size_t itemSize = 0;
                    if (faceProperty.list_item_type_str == "int8" || faceProperty.list_item_type_str == "uint8" || faceProperty.list_item_type_str == "char" || faceProperty.list_item_type_str == "uchar") itemSize = 1;
                    else if (faceProperty.list_item_type_str == "int16" || faceProperty.list_item_type_str == "uint16" || faceProperty.list_item_type_str == "short" || faceProperty.list_item_type_str == "ushort") itemSize = 2;
                    else if (faceProperty.list_item_type_str == "int32" || faceProperty.list_item_type_str == "uint32" || faceProperty.list_item_type_str == "int" || faceProperty.list_item_type_str == "uint") itemSize = 4;
                    if (itemSize > 0) file.seekg(itemSize, ios_base::cur); else { cerr << "无法跳过无效面索引" << endl; return false; }
                }
            }
            continue;
        }

        vector<int> indices(numFaceVertices);
        if (isASCII) {
            // 从同一个istringstream对象中读取所有索引
            istringstream iss_line_for_indices(line);
            int temp_count;
            iss_line_for_indices >> temp_count; // 读取并丢弃计数值
            for (int j = 0; j < numFaceVertices; ++j) {
                if (!(iss_line_for_indices >> indices[j])) {
                    cerr << "错误: 读取ASCII面 " << i << " 的顶点索引 " << j << " 时出错。" << endl;
                    return false;
                }
            }
        }
        else { // 二进制
            for (int j = 0; j < numFaceVertices; ++j) {
                // 根据 faceProperty.list_item_type_str 读取索引
                if (faceProperty.list_item_type_str == "int" || faceProperty.list_item_type_str == "int32") {
                    if (!readBinary(file, indices[j], plyFileIsLittleEndian, systemIsLE)) return false;
                }
                else if (faceProperty.list_item_type_str == "uint" || faceProperty.list_item_type_str == "uint32") {
                    uint32_t temp_idx;
                    if (!readBinary(file, temp_idx, plyFileIsLittleEndian, systemIsLE)) return false;
                    indices[j] = static_cast<int>(temp_idx);
                }
                else if (faceProperty.list_item_type_str == "short" || faceProperty.list_item_type_str == "int16") {
                    int16_t temp_idx;
                    if (!readBinary(file, temp_idx, plyFileIsLittleEndian, systemIsLE)) return false;
                    indices[j] = static_cast<int>(temp_idx);
                }
                else if (faceProperty.list_item_type_str == "ushort" || faceProperty.list_item_type_str == "uint16") {
                    uint16_t temp_idx;
                    if (!readBinary(file, temp_idx, plyFileIsLittleEndian, systemIsLE)) return false;
                    indices[j] = static_cast<int>(temp_idx);
                }
                else if (faceProperty.list_item_type_str == "uchar" || faceProperty.list_item_type_str == "uint8") {
                    unsigned char temp_idx;
                    if (!readBinary(file, temp_idx, plyFileIsLittleEndian, systemIsLE)) return false;
                    indices[j] = static_cast<int>(temp_idx);
                }
                // 添加对其他索引类型的支持
                else {
                    cerr << "错误: 不支持的面索引的二进制类型: " << faceProperty.list_item_type_str << endl;
                    return false;
                }
            }
        }

        if (numFaceVertices == 3) {
            triangles_out.push_back({ indices[0], indices[1], indices[2] });
        }
        else if (numFaceVertices > 3) {
            for (int j = 1; j < numFaceVertices - 1; ++j) {
                triangles_out.push_back({ indices[0], indices[j], indices[j + 1] });
            }
        }
    }
    return true;
}

// 将顶点和三角形数据写入OBJ文件
bool writeOBJ(const string& objPath, const vector<Vertex>& vertices, const vector<Triangle>& triangles,
    bool has_normals, bool has_colors, bool has_texCoords) {
    ofstream file(objPath);
    if (!file.is_open()) {
        cerr << "错误: 无法创建OBJ文件 " << objPath << endl;
        return false;
    }

    file.imbue(std::locale::classic()); // 使用经典C区域设置，确保浮点数用点号表示

    // 写入文件头
    file << "# Converted from PLY to OBJ by PLYtoOBJ_Converter\n";
    file << "# Vertices: " << vertices.size() << "\n";
    file << "# Faces: " << triangles.size() << "\n";
    if (has_normals) file << "# Has Normals\n";
    if (has_colors) file << "# Has Vertex Colors (appended to 'v' lines as r g b)\n";
    if (has_texCoords) file << "# Has Texture Coordinates\n";
    file << "\n";

    // 写入顶点数据 (格式: v x y z [r g b])
    for (const auto& v_data : vertices) {
        file << "v " << v_data.position.x << " " << v_data.position.y << " " << v_data.position.z;
        if (v_data.has_color) { // 使用每个顶点自己的标志
            file << " " << v_data.color.x << " " << v_data.color.y << " " << v_data.color.z;
        }
        file << "\n";
    }
    file << "\n";

    // 写入纹理坐标 (格式: vt u v)
    if (has_texCoords) {
        for (const auto& v_data : vertices) {
            if (v_data.has_texCoord) { // 确保只为有纹理坐标的顶点写入vt
                file << "vt " << v_data.texCoord.u << " " << v_data.texCoord.v << "\n";
            }
            else { // OBJ需要为每个顶点提供vt，如果某些顶点没有，可以输出0 0
                file << "vt 0 0\n";
            }
        }
        file << "\n";
    }

    // 写入法线数据 (格式: vn x y z)
    if (has_normals) {
        for (const auto& v_data : vertices) {
            if (v_data.has_normal) { // 确保只为有法线的顶点写入vn
                file << "vn " << v_data.normal.x << " " << v_data.normal.y << " " << v_data.normal.z << "\n";
            }
            else { // OBJ需要为每个顶点提供vn，如果某些顶点没有，可以输出0 0 1 (默认向上)
                file << "vn 0 0 1\n";
            }
        }
        file << "\n";
    }

    // 写入面数据
    // 格式: f v1[/vt1][/vn1] v2[/vt2][/vn2] v3[/vt3][/vn3]
    // OBJ索引从1开始
    for (const auto& tri : triangles) {
        file << "f";
        for (int k = 0; k < 3; ++k) {
            int v_idx = -1;
            if (k == 0) v_idx = tri.v0;
            else if (k == 1) v_idx = tri.v1;
            else v_idx = tri.v2;

            file << " " << v_idx + 1; // 顶点索引

            if (has_texCoords) {
                file << "/" << v_idx + 1; // 纹理坐标索引 (与顶点索引相同)
            }
            else if (has_normals) { // 如果没有纹理坐标但有法线
                file << "/";
            }


            if (has_normals) {
                file << "/" << v_idx + 1; // 法线索引 (与顶点索引相同)
            }
        }
        file << "\n";
    }

    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        cout << "用法: " << argv[0] << " <输入.ply> <输出.obj>\n";
        cout << "示例: " << argv[0] << " model.ply model.obj\n";
        return 1;
    }

    // 记录总开始时间
    auto total_start_time = std::chrono::high_resolution_clock::now();

    string plyPath = argv[1];
    string objPath = argv[2];

    vector<Vertex> vertices;
    vector<Triangle> triangles;
    bool has_normals, has_colors, has_texCoords;

    cout << "正在转换: " << plyPath << " -> " << objPath << endl;

    // 计时PLY读取
    auto read_start_time = std::chrono::high_resolution_clock::now();
    bool read_success = readPLY(plyPath, vertices, triangles, has_normals, has_colors, has_texCoords);
    auto read_end_time = std::chrono::high_resolution_clock::now();
    auto read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(read_end_time - read_start_time);

    if (!read_success) {
        cerr << "转换失败: PLY文件读取错误或格式不受支持" << endl;
        return 1;
    }

    cout << "读取成功: " << vertices.size() << " 个顶点, "
        << triangles.size() << " 个三角形面" << endl;
    if (has_normals) cout << "  文件包含法线数据." << endl;
    if (has_colors) cout << "  文件包含颜色数据." << endl;
    if (has_texCoords) cout << "  文件包含纹理坐标数据." << endl;
    cout << "PLY读取耗时: " << read_duration.count() << "毫秒" << endl;


    // 计时OBJ写入
    auto write_start_time = std::chrono::high_resolution_clock::now();
    bool write_success = writeOBJ(objPath, vertices, triangles, has_normals, has_colors, has_texCoords);
    auto write_end_time = std::chrono::high_resolution_clock::now();
    auto write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(write_end_time - write_start_time);

    if (!write_success) {
        cerr << "转换失败: OBJ文件写入错误" << endl;
        return 1;
    }

    auto total_end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end_time - total_start_time);

    cout << "OBJ写入耗时: " << write_duration.count() << "毫秒" << endl;
    cout << "转换成功! 已生成OBJ文件: " << objPath << endl;
    cout << "总耗时: " << total_duration.count() << "毫秒" << endl;

    return 0;
}