/*
        This is an example implementation of some of the results described in
   the paper "Desertscapes Simulation".

        No real time visualization in order to reduce dependencies. Running the
   program will output 4 heightfields (jpg files).

        If you have any questions, you can contact me at:
        axel(dot)paris(at)liris(dot)cnrs(dot)fr
*/

#define _CRT_SECURE_NO_WARNINGS

#include "desert.h"

#include <sstream>

/*!
\brief Running this program will export some
meshes similar to the ones seen in the paper.
*/
int main() {
  // Transverse dunes are created under unimodal wind, as well as medium to high
  // sand supply. They are basically the default dune type obtained by any basic
  // simulation scenario.
  std::cout << "Transverse dunes" << std::endl;
  DuneSediment dune =
      DuneSediment(Box2D(Vector2(0), Vector2(1024)), 3.0, 5.0, Vector2(0, 3));

  const int numSteps = 300;
  // Initial
  dune.ExportJPG("transverse_0.jpg");
  for (int i = 1; i <= numSteps; i++) {
    dune.SimulationStepMultiThreadAtomic();
    if ((i % 100) == 0) {
      std::ostringstream ossFilename;
      ossFilename << "transverse_" << i << ".jpg";
      dune.ExportJPG(ossFilename.str());
      std::cout << "\r" << float(i) / numSteps * 100 << "\% done!";
    }
  }
  std::cout << "\n" << std::endl;

  //   // Barchan dunes appears under similar wind conditions, but lower sand
  //   supply.
  std::cout << "Barchan dunes" << std::endl;
  dune.ExportJPG("brachan_0.jpg");
  dune = DuneSediment(Box2D(Vector2(0), Vector2(1024)), 0.5, 2.0, Vector2(0, 5));
    for (int i = 1; i <= 300; i++) {
      dune.SimulationStepMultiThreadAtomic();
        if ((i % 100) == 0) {
          std::ostringstream ossFilename;
          ossFilename << "brachan_" << i << ".jpg";
          dune.ExportJPG(ossFilename.str());
          std::cout << "\r" << float(i) / numSteps * 100 << "\% done!";
        }
    }
    std::cout << "\n" << std::endl;

  //   // Yardangs are created by abrasion, activated with a specific flag in
  //   our
  //   // simulation. Note: for more rounded yardangs, a turbulent wind is
  //   neccesary. std::cout << "Yardangs" << std::endl; dune =
  //       DuneSediment(Box2D(Vector2(0), Vector2(1024)), 0.5, 0.5, Vector2(6,
  //       0));
  //   dune.SetAbrasionMode(true);
  //   for (int i = 0; i < 600; i++)
  //     dune.SimulationStepMultiThreadAtomic();
  //   dune.ExportJPG("yardangs.jpg");
  //   std::cout << "Done 3/4" << std::endl << std::endl;

  //   // Nabkha are created under the influence of vegetation, also a flag to
  //   turn
  //   // on.
  //   std::cout << "Nabkha" << std::endl;
  //   dune =
  //       DuneSediment(Box2D(Vector2(0), Vector2(1024)), 2.0, 5.0, Vector2(3,
  //       0));
  //   dune.SetVegetationMode(true);
  //   for (int i = 0; i < 300; i++)
  //     dune.SimulationStepMultiThreadAtomic();
  //   dune.ExportJPG("nabkha.jpg");
  //   std::cout << "Done 4/4" << std::endl << std::endl;

  return 0;
}
