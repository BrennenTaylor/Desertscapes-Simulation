#include "desert.h"
#include "noise.h"

#include <fstream>
#include <iostream>
#include <random>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/*!
\brief Default constructor.
*/
DuneSediment::DuneSediment() {
  nx = ny = 1024;
  box = Box2D(Vector2(0), 1);
  wind = Vector2(1, 0);

  bedrock = ScalarField2D(nx, ny, box, 0.0);
  vegetation = ScalarField2D(nx, ny, box, 0.0);
  sediments = ScalarField2D(nx, ny, box, 0.0);

  matterToMove = 0.1f;
  Vector2 celldiagonal =
      Vector2((box.TopRight()[0] - box.BottomLeft()[0]) / (nx - 1),
              (box.TopRight()[1] - box.BottomLeft()[1]) / (ny - 1));
  cellSize = Box2D(box.BottomLeft(), box.BottomLeft() + celldiagonal)
                 .Size()
                 .x; // We only consider squared heightfields
}

/*!
\brief Constructor.
\param bbox 2D bounding box
\param rMin min amount of sediment per cell
\param rMax max amount of sediment per cell
\param w wind vector
*/
DuneSediment::DuneSediment(const Box2D &bbox, float rMin, float rMax,
                           const Vector2 &w) {
  box = bbox;
  nx = ny = 1024;
  wind = w;

  std::mt19937_64 gen(0);
  std::uniform_real_distribution<float> uniformSand(rMin, rMax);

  bedrock = ScalarField2D(nx, ny, box, 0.0);
  vegetation = ScalarField2D(nx, ny, box, 0.0);
  sediments = ScalarField2D(nx, ny, box, 0.0);
  for (int i = 0; i < nx; i++) {
    for (int j = 0; j < ny; j++) {
      Vector2 p = bedrock.ArrayVertex(i, j);

      //   // Vegetation
      //   // Arbitrary clamped 2D noise - but you can use whatever you want.
      //   float v = PerlinNoise::fBm(Vector3(i * 7.91247f, j * 7.91247f, 0.0f),
      //                              1.0f, 0.002f, 3) /
      //             1.75f;
      //   if (v > 0.45f)
      //     vegetation.Set(i, j, 0.85f);

      // Sand
      sediments.Set(i, j, uniformSand(gen));
    }
  }

  // By default, vegetation influence and abrasion are turned off.
  vegetationOn = false;
  abrasionOn = false;

  Vector2 celldiagonal =
      Vector2((box.TopRight()[0] - box.BottomLeft()[0]) / (nx - 1),
              (box.TopRight()[1] - box.BottomLeft()[1]) / (ny - 1));
  cellSize = Box2D(box.BottomLeft(), box.BottomLeft() + celldiagonal)
                 .Size()
                 .x; // We only consider squared heightfields

  matterToMove = 0.1f;
}

/*!
\brief Destructor.
*/
DuneSediment::~DuneSediment() {}

/*!
\brief Export the current dune model as an obj file representing the full
heightfield. \param url file path
*/
void DuneSediment::ExportObj(const std::string &url) const {
  // Clear old data
  std::vector<Vector3> vertices;
  std::vector<Vector3> normals;
  std::vector<int> indices;

  // Vertices & UVs & Normals
  normals.resize(nx * ny, Vector3(0));
  vertices.resize(nx * ny, Vector3(0));
  for (int i = 0; i < nx; i++) {
    for (int j = 0; j < ny; j++) {
      int id = ToIndex1D(i, j);
      normals[id] =
          -Normalize(Vector2(bedrock.Gradient(i, j) + sediments.Gradient(i, j))
                         .ToVector3(-2.0f));
      vertices[id] = Vector3(
          box[0][0] + i * (box[1][0] - box[0][0]) / (nx - 1), Height(i, j),
          box[0][1] + j * (box[1][1] - box[0][1]) / (ny - 1));
    }
  }

  // Triangles
  int c = 0;
  int vertexArrayLength = ny * nx;
  while (c < vertexArrayLength - nx - 1) {
    if (c == 0 || (((c + 1) % nx != 0) && c <= vertexArrayLength - nx)) {
      indices.push_back(c + nx + 1);
      indices.push_back(c + nx);
      indices.push_back(c);

      indices.push_back(c);
      indices.push_back(c + 1);
      indices.push_back(c + nx + 1);
    }
    c++;
  }

  // Export as .obj file
  std::ofstream out;
  out.open(url);
  if (out.is_open() == false)
    return;
  out << "g "
      << "Obj" << std::endl;
  for (int i = 0; i < vertices.size(); i++)
    out << "v " << vertices.at(i).x << " " << vertices.at(i).y << " "
        << vertices.at(i).z << '\n';
  for (int i = 0; i < normals.size(); i++)
    out << "vn " << normals.at(i).x << " " << normals.at(i).z << " "
        << normals.at(i).y << '\n';
  for (int i = 0; i < indices.size(); i += 3) {
    out << "f " << indices.at(i) + 1 << "//" << indices.at(i) + 1 << " "
        << indices.at(i + 1) + 1 << "//" << indices.at(i + 1) + 1 << " "
        << indices.at(i + 2) + 1 << "//" << indices.at(i + 2) + 1 << '\n';
  }
  out.close();
}

/*!
\brief Export the current dune model as a jpg file.
\param url file path
*/
void DuneSediment::ExportJPG(const std::string &url) const {
  float min = bedrock.Min() - sediments.Min();
  float max = bedrock.Max() + sediments.Max();
  uint8_t *pixels = new uint8_t[nx * ny * 3];
  int index = 0;
  for (int j = 0; j < ny; j++) {
    for (int i = 0; i < nx; i++) {
      float h = Math::Step(Height(i, j), min, max);
      int hi = int(255.99 * h);
      pixels[index++] = hi;
      pixels[index++] = hi;
      pixels[index++] = hi;
    }
  }
  stbi_write_jpg(url.c_str(), nx, ny, 3, pixels, 98);
}
