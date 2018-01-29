//Contains different sizing fields structures for different proposes: Spherical, Elliptical, planar gradient
// Refer to original CGAL documents if you would like to know more about sizing fields
// developed by Kirill Aristovich
// Example change by James H
//THIS CODE SUCKS!!!!!!!!

#include "Sizing_fields.h"
using namespace std;

Sizing_field::Sizing_field(Point& origin_in, string path_electrode, std::map<std::string, FT> opts)
{

  FILE *F;
  try { F=fopen(path_electrode.c_str(),"r");}
  catch (exception& e) { cout << e.what() << endl;}

  options = opts;
  origin = origin_in;

  ub_x=origin.x();
  ub_y=origin.y();
  ub_z=origin.z();

  FT scale_xyz = 1/options["pixel_scale_mm"];

  // Reallocate some parameters to have less verbose names
  coarse_size = options["cell_coarse_size_mm"];
  fine_size = options["cell_fine_size_mm"];
  preserve = int(options["elements_with_fine_sizing_field_percentage"]);
  e_R = 2 * options["electrode_radius_mm"]; //2* to secure fit of the electrode
  electrode_size = options["cell_size_electrodes_mm"];//Planar gradient with electrodes -- size of the mesh near electrodes

  if (options["planar_refinement"])

  {

    if( options["planar_xyz"]==1 ) {
      options["height"]  *= options["vx"];
      options["upper_bound"]  = options["vx"] * options["xdim"];
    }

    else if( options["planar_xyz"]==2 ) {
      options["height"]  *= options["vy"];
      options["upper_bound"]  = options["vy"] * options["ydim"];
    }

    else if( options["planar_xyz"]==3 ) {
      options["height"]  *= options["vz"];
      options["upper_bound"]  = options["vz"] * options["zdim"];
    }

    else { // Invalid parameter passed
      cout << "Invalid planar direction specified, should be 1, 2 or 3" << endl;
      exit(0);
    }

  }

  // Load electrode positions
  if (F == NULL) perror ("Error opening electrode file");
  else {
    while(!feof(F))
    {
      float x,y,z;
      fscanf(F,"%f,%f,%f\n",&x,&y,&z);
      Point pt(x*scale_xyz,y*scale_xyz,z*scale_xyz);
      centres.push_back(pt);
    }
  }

  if (F!=NULL) fclose(F);

}


FT Sizing_field::operator()(const Point& p, const int, const Index&) const

{
  // Mesh the electrodes
  FT out;
  Points::const_iterator it;
  for (it=centres.begin(); it<centres.end(); it++)
  {
    Vector pp=(p-*it);
    if (pp.squared_length()<=e_R*e_R)
    {

      out=electrode_size;
      return out;
    }
  }

  double distance;
  // Do some additional refienments if turned on in parameter file
  // Need to use MAP.at("x") rather than MAP["x"] to be const safe

  if (options.at("sphere_refinement") ) {
    // Refine a sphere around a specificed point.

    Point sphere_centre(  options.at("sphere_centre_x"),
                          options.at("sphere_centre_y"),
                          options.at("sphere_centre_z"));

    distance = CGAL::sqrt( CGAL::squared_distance(p, sphere_centre) );

    if ( distance < FT(options.at("sphere_radius")) ) {

      out = options.at("sphere_cell_size");
    }

    else {
      out = coarse_size;
    }

  }

  else if (options.at("planar_refinement"))   {

    if(options.at("planar_xyz") == 1) {
      distance=CGAL::abs(p.x()- options.at("height"));
  }

    else if(options.at("planar_xyz") == 2) {
      distance=CGAL::abs(p.y()- options.at("height"));
    }

    else if(options.at("planar_xyz") == 3) {
      distance=CGAL::abs(p.z()- options.at("height"));
    }


    else {
      return fine_size;
    }

    double dist_percentage = distance / double(options.at("upper_bound")) ;

    if (dist_percentage <= (preserve)/100.0) {
      out=fine_size;
    }


    else  {
      out = fine_size +
       CGAL::abs((coarse_size-fine_size)*(dist_percentage - (preserve)/100.0));
  }

  }

  else { //refine centre of mesh more than outside

    //TODO: Not sure if this algorithm is legit
    // Cartersian distance from centre of the mesh
    Vector distance_eliptic = p - origin;
    FT distance_percent = CGAL::sqrt(     (distance_eliptic.x()/origin.x()) * (distance_eliptic.x()/origin.x()) +
                                          (distance_eliptic.y()/origin.y()) * (distance_eliptic.y()/origin.y()) +
                                          (distance_eliptic.z()/origin.z()) * (distance_eliptic.z()/origin.z())  );

    if (distance_percent >= 1-FT(preserve)/100) {
      out=fine_size + (coarse_size-fine_size) * (1-distance_percent);
    }

    else {
      out=fine_size;
    }


  }
  return out;
}