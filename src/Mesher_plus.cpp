
#include "Sizing_fields.h"
#include "input_parameters.h"
#include "Matlab_save.h"
#include "save_dgf.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// include/defines used across all  statements
#include "include.h"

// To avoid verbose function and named parameters call
using namespace CGAL::parameters;
using namespace std;

#include <CGAL/config.h>

#include "from_matlab.h"

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

// Print CGAL Version number
        std::cout << "CGAL Version " << CGAL_VERSION_NR << " (1MMmmb1000)" << std::endl;
        std::cout << "where MM is the major number release, mm is the minor number release" << std::endl;

// Check input parameters

        if(argc < 10) printusage();
        int opt;
        char *path_image, *path_electrode, *path_parameter;
        std::string output_dir, mesh_name, output_file, electrode_file, parameter_file;
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
                        mesh_name = optarg;

                }
        }
        if(optind != argc) printusage();

        // Output file names for sanity check
        std::cout << "Input file: "     << path_image << "\n";
        std::cout << "Electrode file: "   << path_electrode << "\n";
        std::cout << "Parameter file: "   << path_parameter << "\n";
        std::cout << "Output directory: "   << output_dir << "\n";
        std::cout << "Output mesh name: "    << mesh_name << "\n\n";

        // Build filenames for electrode positions and parameters
        electrode_file = output_dir + "electrode_positions_" + mesh_name;
        parameter_file = output_dir +"param_" + mesh_name;
        output_file = output_dir + mesh_name + ".dgf";

        // Loads image
        CGAL::Image_3 image;

        // Read input file with parameters
        Input p;
        p.load_file_idx(path_parameter);

        // Reading image file
        std::cout<<"\n Reading the Image file... ";
        image.read(path_image);


        // Domain
        Mesh_domain domain(image);

        //Define Sizing field
        // vx, vy and vz are the size of each voxel
        // xdim, ydim and zdim are the number of voxels along each axis
        Point origin(image.vx () * image.xdim ()/2,
                     image.vy () * image.ydim ()/2,
                     image.vz () * image.zdim ()/2); //origin

        FILE *F;
        try { F=fopen(path_electrode,"r");}
        catch (exception& e) { cout << e.what() << endl;}

        Mesh_domain::Index sub = domain.index_from_subdomain_index(2); //!!! I do not remember what this does, but it should be very useful ...

        sizing_field_elliptic_electrodes sizing_field (origin,F,1/p.unit,1/p.unit,1/p.unit); //This is basic and working now for both rat and human

        if (F!=NULL) fclose(F);

        sizing_field.coarse_size=p.options["cell_coarse_size_mm"];
        sizing_field.fine_size=p.options["cell_fine_size_mm"];
        sizing_field.preserve=int(p.options["elements_with_fine_sizing_fieldercentage"]);
        sizing_field.e_R=2*p.options["electrode_radius_mm"]; //2* to secure fit of the electrode
        sizing_field.electrode_size=p.options["cell_size_electrodes_mm"];//Planar gradient with electrodes -- size of the mesh near electrodes


        // Mesh criteria: faces and cells
        Mesh_criteria criteria(facet_angle=p.options["facet_angle_deg"], facet_size=sizing_field, facet_distance=p.options["facet_distance_mm"],
                               cell_radius_edge_ratio=p.options["cell_radius_edge_ratio"], cell_size=sizing_field);


        // Meshing
        std::cout<<"\n Meshing with initial mesh..." << endl;
        C3t3 c3t3;
        c3t3= CGAL::make_mesh_3<C3t3>(domain, criteria, CGAL::parameters::features(domain),
                                      CGAL::parameters::no_lloyd(), CGAL::parameters::no_odt(),
                                      CGAL::parameters::no_perturb(),CGAL::parameters::no_exude());

        // Generate reference electrode location and append to elecrtode list
        Point reference_electrode = set_reference_electrode(c3t3);
        sizing_field.centres.push_back(reference_electrode);

        Point ground_electrode = set_ground_electrode(c3t3);

        cout << "Moving electrodes to closest facets: " << endl;
        for(int i = 0; i < sizing_field.centres.size(); ++i) {
                sizing_field.centres[i] = closest_element(c3t3, sizing_field.centres[i]);
        }
        cout << "Finished moving electrodes" << endl;

        //Optimisation
        std::cout<<"\n Optimising: " << endl;
        if (int(p.options["perturb_opt"])==1) {std::cout<<"\n Perturb... "; CGAL::perturb_mesh_3(c3t3, domain,sliver_bound=10, time_limit=p.options["time_limit_sec"]);}
        if (int(p.options["lloyd_opt"])==1)  {std::cout<<"\n Lloyd... "; CGAL::lloyd_optimize_mesh_3(c3t3, domain, time_limit=p.options["time_limit_sec"]);}
        if (int(p.options["odt_opt"])==1)  {std::cout<<"\n ODT... "; CGAL::odt_optimize_mesh_3(c3t3, domain, time_limit=p.options["time_limit_sec"]);}
        if (int(p.options["exude_opt"])==1)  {std::cout<<"\n Exude... "; CGAL::exude_mesh_3(c3t3, sliver_bound=10, time_limit=p.options["time_limit_sec"]);}

        // Put together parameters
        std::map<std::string, std::string> parameters;
        parameters["fem.io.macroGrid"] = output_file;
        parameters["electrode.use_node_assignment"] = string("false");
        parameters["electrode.positions"] = electrode_file;
        // I don't think the below two are needed
        //parameters["electrode.nodes"]
        //parameters["surface.coordinates"]
        parameters["ground.hsquared"] = string("1.5e-5");

        // Need to convert double to string before adding to parameter map
        // using std::ostringstream to do this
        std::ostringstream gndposx, gndposy, gndposz;
        gndposx << CGAL::to_double(ground_electrode.x());
        gndposy << CGAL::to_double(ground_electrode.y());
        gndposz << CGAL::to_double(ground_electrode.z());

        parameters["groundposition.x"] = gndposx.str();
        parameters["groundposition.y"] = gndposy.str();
        parameters["groundposition.z"] = gndposz.str();

        // TODO: modify below depnding on whether integer values for conductivitry are given
        parameters["fem.assign_conductivities"] = string("false");

        //all done
        std::cout<<"\n ALL DONE! :)" << endl;

        // Output dgf file and electrode_positions
        save_as_dgf(c3t3, p, output_file);
        save_electrodes(sizing_field.centres, electrode_file);
        save_parameters(parameters, parameter_file);
        return 0;
}
