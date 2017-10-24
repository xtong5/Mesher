// include/defines used across all  statements
#include "CGAL_include.h"

#include "input_parameters.h"
#include "Sizing_fields.h"
#include "mesh_operations.h"
#include "save_dgf.h"
#include "warp_mesh.h"

#include "write_c3t3_to_vtk_xml_file.h"

// To avoid verbose function and named parameters call
using namespace CGAL::parameters;
using namespace std;

void printusage(void)
{
  printf("Usage:  -i input image file\n");
  printf("        -e electrode position file\n");
  printf("        -p parameter file\n");
  printf("        -o output mesh name\n");
  printf("        -d output directory\n");
  exit(EXIT_FAILURE);
}


int main(int argc, char* argv[])
{

  //TODO: If the inr file has units of mm, the output mesh will also be in mme.
  //Forward solever wants metres, so need to convert. Doing this expilcity at the moment with
  // MM_TO_M, but is there a nicer way
  // Print CGAL Version number
  std::cout << "CGAL Version " << CGAL_VERSION_NR << " (1MMmmb1000)" << std::endl;
  std::cout << "where MM is the major number release, mm is the minor number release" << std::endl;

  // Process input parameters

  if(argc < 10) printusage();
  int opt;
  char *path_image, *path_electrode, *path_parameter;
  string        output_dir, input_mesh_name,  output_mesh_name, output_base_file;

  while((opt = getopt(argc, argv, "i:e:p:o:d:"))!=-1)
  {
    switch(opt)
    {
      case 'i':
      path_image = optarg;
      case 'e':
      path_electrode = optarg;
      case 'p':
      path_parameter = optarg;

      case 'd':
      output_dir = optarg;
      case 'o':
      input_mesh_name = optarg;

    }
  }

  if(optind != argc) printusage();

  // Output file names for sanity check
  std::cout << "Input file: "     << path_image << "\n";
  std::cout << "Electrode file: "   << path_electrode << "\n";
  std::cout << "Parameter file: "   << path_parameter << "\n";
  std::cout << "Output directory: "   << output_dir << "\n";
  std::cout << "Input mesh name: "    << input_mesh_name << "\n\n";

  // Read input file with parameters
  Input p;
  p.load_file_idx(path_parameter);

  // How many deformations to do (if any)
  int n_deformations = p.options["num_deformations"];
  bool do_deform = (n_deformations > 0); // True if > 0

  if (do_deform) {
    cout << "Mesh deformation turned on. " <<  n_deformations << " meshes will be generated" << endl;
  }

  do {
    // Loads image
    CGAL::Image_3 image;
    std::cout<<"\n Reading the Image file... ";

    image.read(path_image);
    cout << "Dimensions of image: " << image.xdim() << endl;

    // Set to default value
    output_mesh_name = input_mesh_name;

    if (do_deform) {
      unsigned char * image_data = (unsigned char*)image.data();
      Deform_Volume warper(image.data(), image.xdim());
      warper.modify_image();

      // Append mesh_name with details of deformation
      output_mesh_name = input_mesh_name + warper.deformation_info;
      cout << "New mesh name: " << output_mesh_name << endl;

    }

    // Domain
    Mesh_domain domain(image);

    //Define Sizing field
    // vx, vy and vz are the size of each voxel
    // xdim, ydim and zdim are the number of voxels along each axis
    Point origin(image.vx () * image.xdim ()/2,
    image.vy () * image.ydim ()/2,
    image.vz () * image.zdim ()/2); //origin

    sizing_field_elliptic_electrodes sizing_field (origin,path_electrode,p); //This is basic and working now for both rat and human

    // Mesh criteria: faces and cells
    Mesh_criteria criteria(facet_angle=p.options["facet_angle_deg"], facet_size=sizing_field, facet_distance=p.options["facet_distance_mm"],
    cell_radius_edge_ratio=p.options["cell_radius_edge_ratio"], cell_size=sizing_field);


    // Meshing
    std::cout<<"\n Meshing with initial mesh..." << endl;
    C3t3 c3t3;
    c3t3= CGAL::make_mesh_3<C3t3>(domain, criteria, CGAL::parameters::features(domain),
    CGAL::parameters::no_lloyd(), CGAL::parameters::no_odt(),
    CGAL::parameters::no_perturb(),CGAL::parameters::no_exude());

    //Optimisation
    std::cout<<"\n Optimising: " << endl;
    if (int(p.options["perturb_opt"])==1) {
      std::cout<<"\n Perturb... ";
      CGAL::perturb_mesh_3(c3t3, domain,sliver_bound=10, time_limit=p.options["time_limit_sec"]);
    }

    if (int(p.options["lloyd_opt"])==1) {
      std::cout<<"\n Lloyd... ";
      CGAL::lloyd_optimize_mesh_3(c3t3, domain, time_limit=p.options["time_limit_sec"]);
    }

    if (int(p.options["odt_opt"])==1) {
      std::cout<<"\n ODT... ";
      CGAL::odt_optimize_mesh_3(c3t3, domain, time_limit=p.options["time_limit_sec"]);
    }

    if (int(p.options["exude_opt"])==1)  {
      std::cout<<"\n Exude... ";
      CGAL::exude_mesh_3(c3t3, sliver_bound=10, time_limit=p.options["time_limit_sec"]);
    }


    // Generate reference electrode location and append to elecrtode list
    Point reference_electrode = set_reference_electrode(c3t3);
    sizing_field.centres.push_back(reference_electrode);

    Point ground_electrode = set_ground_electrode(c3t3);

    cout << "Moving electrodes to closest facets: " << endl;
    for(int i = 0; i < sizing_field.centres.size(); ++i) {
      sizing_field.centres[i] = closest_element(c3t3, sizing_field.centres[i]);
    }
    cout << "Finished moving electrodes" << endl;

    // Put together parameters
    std::map<std::string, std::string> parameters;

    parameters["ground.hsquared"] = string("1.5e-5");

    // Need to convert double to string before adding to parameter map
    // using std::ostringstream to do this
    std::ostringstream gndposx, gndposy, gndposz;
    gndposx << CGAL::to_double(ground_electrode.x())/MM_TO_M;
    gndposy << CGAL::to_double(ground_electrode.y())/MM_TO_M;
    gndposz << CGAL::to_double(ground_electrode.z())/MM_TO_M;

    parameters["groundposition.x"] = gndposx.str();
    parameters["groundposition.y"] = gndposy.str();
    parameters["groundposition.z"] = gndposz.str();


    //all done
    std::cout<<"\n ALL DONE! :)" << endl;


    // Base filenames for electrode positions and parameters
    output_base_file = output_dir + output_mesh_name;

    // Output dgf file and electrode_positions
    save_as_dgf(c3t3, p, output_base_file);
    save_electrodes(sizing_field.centres, output_base_file);
    save_parameters(parameters, output_base_file);
    write_centres(c3t3, output_base_file);

    // Output the mesh for Paraview
    // TODO: Since outputting everything in metres, rather than mm
    // The vtk file is still being written in mm, as the mesh data is only changed
    // at the point it is written. Fix this
    string vtk_file_path = output_base_file + ".vtu";
    int vtk_success = write_c3t3_to_vtk_xml_file(c3t3, vtk_file_path);
  }
  while (n_deformations-- > 1);

  return 0;
}
