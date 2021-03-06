/*
Version of poisson1d that skip certain cells as defined by user.

Copyright 2013, 2014, 2015,
2016, 2019 Finnish Meteorological Institute

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License version 3
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "cmath"
#include "cstdlib"
#include "iostream"
#include "cstdint"
#include "vector"

#include "mpi.h"
#include "zoltan.h"

#include "dccrg.hpp"
#include "dccrg_cartesian_geometry.hpp"
#include "poisson_solve.hpp"

using namespace std;
using namespace dccrg;


int Poisson_Cell::transfer_switch = Poisson_Cell::INIT;


double get_rhs_value(const double x)
{
	return sin(x);
}


/*
Returns the analytic solution to the test Poisson's equation.
*/
double get_solution_value(const double x)
{
	return -sin(x);
}

/*
Returns the p-norm of the difference of solution from exact.

Given offset is added to the exact solution before calculating the norm.
*/
template<class Geometry> double get_p_norm(
	const dccrg::Dccrg<Poisson_Cell, Geometry>& grid,
	const double p_of_norm
) {
	double local = 0, global = 0;

	for(const auto& cell: grid.local_cells()) {
		const double coord = grid.geometry.get_center(cell.id)[0];
		local += std::pow(
			fabs(cell.data->solution - get_solution_value(coord)),
			p_of_norm
		);
	}
	MPI_Comm temp = grid.get_communicator();
	MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, temp);
	MPI_Comm_free(&temp);
	global = std::pow(global, 1.0 / p_of_norm);

	return global;
}


int main(int argc, char* argv[])
{
	if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
		cerr << "Coudln't initialize MPI." << endl;
		abort();
	}

	MPI_Comm comm = MPI_COMM_WORLD;

	int rank = 0, comm_size = 0;
	MPI_Comm_rank(comm, &rank);
	MPI_Comm_size(comm, &comm_size);

	float zoltan_version;
	if (Zoltan_Initialize(argc, argv, &zoltan_version) != ZOLTAN_OK) {
	    cerr << "Zoltan_Initialize failed" << endl;
	    return EXIT_FAILURE;
	}

	int success = 0, global_success = 0; // > 0 means failure

	double old_norm = std::numeric_limits<double>::max();
	size_t old_number_of_cells = 1;

	const size_t max_number_of_cells = 32768;
	for (size_t
		number_of_cells = 8;
		number_of_cells <= max_number_of_cells;
		number_of_cells *= 2
	) {
		const double cell_length = 2 * M_PI / number_of_cells;

		Poisson_Solve solver(10, 0, 1e-15, 2, 10, false);
		dccrg::Dccrg<
			Poisson_Cell,
			dccrg::Cartesian_Geometry
		> grid, grid_reference;

		dccrg::Cartesian_Geometry::Parameters geom_params;
		geom_params.start[0] =
		geom_params.start[1] =
		geom_params.start[2] = 0;
		geom_params.level_0_cell_length[0] = cell_length;
		geom_params.level_0_cell_length[1] = 1;
		geom_params.level_0_cell_length[2] = 1;

		grid          .set_geometry(geom_params);
		grid_reference.set_geometry(geom_params);

		const std::array<uint64_t, 3>
			// create an extra layer of cells to skip in non-x dimensions
			grid_length = {{number_of_cells, 3, 3}},
			grid_reference_length = {{number_of_cells, 1, 1}};

		grid
			.set_initial_length(grid_length)
			.set_neighborhood_length(0)
			.set_maximum_refinement_level(0)
			.set_load_balancing_method("RCB")
			.set_periodic(true, false, false)
			.initialize(comm);
		grid_reference
			.set_initial_length(grid_reference_length)
			.set_neighborhood_length(0)
			.set_maximum_refinement_level(0)
			.set_load_balancing_method("RCB")
			.set_periodic(true, false, false)
			.initialize(comm);

		// emulate RANDOM but predictable load balance
		for(const auto& cell: grid.local_cells()) {
			const dccrg::Types<3>::indices_t indices = grid.mapping.get_indices(cell.id);
			// move cells in middle of grid to same process as in reference grid
			if (indices[1] == 1 && indices[2] == 1) {
				const int target_process = indices[0] % comm_size;
				grid.pin(cell.id, target_process);
			}
		}
		for(const auto& cell: grid_reference.local_cells()) {
			const dccrg::Types<3>::indices_t indices = grid_reference.mapping.get_indices(cell.id);
			const int target_process = indices[0] % comm_size;
			grid_reference.pin(cell.id, target_process);
		}
		grid          .balance_load(false);
		grid_reference.balance_load(false);
		grid          .unpin_all_cells();
		grid_reference.unpin_all_cells();

		// get cells in which to solve and boundary cells to skip
		const std::vector<uint64_t>
			cells_reference = grid_reference.get_cells(),
			all_cells = grid.get_cells();
		std::vector<uint64_t> cells, cells_to_skip;

		for(const auto& cell: all_cells) {
			const dccrg::Types<3>::indices_t indices = grid.mapping.get_indices(cell);
			if (indices[1] == 1 && indices[2] == 1) {
				cells.push_back(cell);
			} else {
				cells_to_skip.push_back(cell);
			}
		}

		if (cells.size() != cells_reference.size()) {
			std::cerr << __FILE__ << ":" << __LINE__
				<< " Length of cell lists not equal: " << cells.size() << " (";
			for(const auto& cell: cells) {
				cout << cell << ",";
			}
			cout << "); should be " << cells_reference.size() << " (";
			for(const auto& cell: cells_reference) {
				cout << cell << ",";
			}
			cout << ")" << std::endl;
			abort();
		}

		// initialize
		for(const auto& cell: cells) {
			Poisson_Cell *data = grid[cell];
			if (data == NULL) {
				std::cerr << __FILE__ << ":" << __LINE__ << std::endl;
				abort();
			}
			const double coord = grid.geometry.get_center(cell)[0];
			data->rhs = get_rhs_value(coord);
			data->solution = 0;
		}
		for(const auto& cell: cells_reference) {
			Poisson_Cell *data = grid_reference[cell];
			if (data == NULL) {
				std::cerr << __FILE__ << ":" << __LINE__ << std::endl;
				abort();
			}
			const double coord = grid_reference.geometry.get_center(cell)[0];
			data->rhs = get_rhs_value(coord);
			data->solution = 0;
		}

		solver.solve(cells, grid, cells_to_skip);
		solver.solve(cells_reference, grid_reference);

		// check that parallel solutions are close to analytic
		const double
			p_of_norm = 2,
			norm = get_p_norm(grid, p_of_norm),
			norm_reference = get_p_norm(grid_reference, p_of_norm);

		if (norm > old_norm) {
			success = 1;
			if (rank == 0) {
				std::cerr << __FILE__ << ":" << __LINE__
					<< " " << p_of_norm
					<< " norm of solution larger from exact with " << number_of_cells
					<< " cells (" << norm << ") than with " << old_number_of_cells
					<< " cells (" << old_norm << ")"
					<< std::endl;
			}
		}
		if (norm_reference > old_norm) {
			success = 1;
			if (rank == 0) {
				std::cerr << __FILE__ << ":" << __LINE__
					<< " " << p_of_norm
					<< " norm of reference solution larger from exact with " << number_of_cells
					<< " cells (" << norm_reference << ") than with " << old_number_of_cells
					<< " cells"
					<< std::endl;
			}
		}

		old_norm = norm;
		old_number_of_cells = number_of_cells;

		MPI_Allreduce(&success, &global_success, 1, MPI_INT, MPI_SUM, comm);
		if (global_success > 0) {
			break;
		}
	}

	MPI_Finalize();

	if (success == 0) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}
