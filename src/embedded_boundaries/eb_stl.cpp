#include <AMReX_EB2.H>
#include <AMReX_EB2_IF.H>
#include <AMReX_EB_utils.H>
#include <AMReX_EB_STL_utils.H>
#include <AMReX_ParmParse.H>

#include <incflo.H>

using namespace amrex;

void incflo::make_eb_stl(const std::string& stl_file)
{

	// auto gshop = EB2::makeShop(my_box);

	// build a stl geometry using the "eb2." paramenters in the inputs file
    int max_level_here = 0;
    int max_coarsening_level = 100;
    EB2::Build(geom.back(), max_level_here, max_level_here + max_coarsening_level);
}