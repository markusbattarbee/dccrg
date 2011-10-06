/*
Tests the grid with some simple game of life patters using a hierarchial load balancing method.
Returns EXIT_SUCCESS if everything went ok.
*/

#include "algorithm"
#include "boost/mpi.hpp"
#include "boost/unordered_set.hpp"
#include "cstdlib"
#include "fstream"
#include "iostream"
#include "zoltan.h"

#include "../../dccrg.hpp"


struct game_of_life_cell {

	template<typename Archiver> void serialize(Archiver& ar, const unsigned int /*version*/) {
		ar & is_alive;
	}

	bool is_alive;
	unsigned int live_neighbour_count;
};


using namespace std;
using namespace boost::mpi;
using namespace dccrg;


int main(int argc, char* argv[])
{
	environment env(argc, argv);
	communicator comm;

	float zoltan_version;
	if (Zoltan_Initialize(argc, argv, &zoltan_version) != ZOLTAN_OK) {
	    cout << "Zoltan_Initialize failed" << endl;
	    exit(EXIT_FAILURE);
	}
	if (comm.rank() == 0) {
		cout << "Using Zoltan version " << zoltan_version << endl;
	}

	Dccrg<game_of_life_cell> game_grid(comm, "HIER", 0, 0, 0, 1, 1, 1, 34, 7, 1, 1);
	if (comm.rank() == 0) {
		cout << "Maximum refinement level of the grid: " << game_grid.get_max_refinement_level() << endl;
	}

	game_grid.add_partitioning_level(12);
	game_grid.add_partitioning_option(0, "LB_METHOD", "HYPERGRAPH");
	game_grid.add_partitioning_option(0, "IMBALANCE_TOL", "1.05");

	game_grid.add_partitioning_level(1);
	game_grid.add_partitioning_option(1, "LB_METHOD", "HYPERGRAPH");

	game_grid.balance_load();
	vector<uint64_t> cells = game_grid.get_cells();
	sort(cells.begin(), cells.end());

	// initialize the game
	for (vector<uint64_t>::const_iterator cell = cells.begin(); cell != cells.end(); cell++) {

		game_of_life_cell* cell_data = game_grid[*cell];
		cell_data->live_neighbour_count = 0;

		if (double(rand()) / RAND_MAX < 0.2) {
			cell_data->is_alive = true;
		} else {
			cell_data->is_alive = false;
		}
	}

	// every process outputs the game state into its own file
	ostringstream basename, suffix(".vtk");
	basename << "hierarchial_test_" << comm.rank() << "_";
	ofstream outfile, visit_file;

	// visualize the game with visit -o game_of_life_test.visit
	if (comm.rank() == 0) {
		visit_file.open("hierarchial_test.visit");
		visit_file << "!NBLOCKS " << comm.size() << endl;
		cout << "step: ";
	}

	#define TIME_STEPS 25
	for (int step = 0; step < TIME_STEPS; step++) {

		game_grid.start_remote_neighbour_data_update();
		game_grid.wait_neighbour_data_update();

		if (comm.rank() == 0) {
			cout << step << " ";
			cout.flush();
		}

		// write the game state into a file named according to the current time step
		string current_output_name("");
		ostringstream step_string;
		step_string.fill('0');
		step_string.width(5);
		step_string << step;
		current_output_name += basename.str();
		current_output_name += step_string.str();
		current_output_name += suffix.str();

		// visualize the game with visit -o game_of_life_test.visit
		if (comm.rank() == 0) {
			for (int process = 0; process < comm.size(); process++) {
				visit_file << "hierarchial_test_" << process << "_" << step_string.str() << suffix.str() << endl;
			}
		}


		// write the grid into a file
		game_grid.write_vtk_file(current_output_name.c_str());
		// prepare to write the game data into the same file
		outfile.open(current_output_name.c_str(), ofstream::app);
		outfile << "CELL_DATA " << cells.size() << endl;

		// go through the grids cells and write their state into the file
		outfile << "SCALARS is_alive float 1" << endl;
		outfile << "LOOKUP_TABLE default" << endl;
		for (vector<uint64_t>::const_iterator cell = cells.begin(); cell != cells.end(); cell++) {

			game_of_life_cell* cell_data = game_grid[*cell];

			if (cell_data->is_alive == true) {
				outfile << "1";
			} else {
				outfile << "0";
			}
			outfile << endl;

		}

		// write each cells live neighbour count
		outfile << "SCALARS live_neighbour_count float 1" << endl;
		outfile << "LOOKUP_TABLE default" << endl;
		for (vector<uint64_t>::const_iterator cell = cells.begin(); cell != cells.end(); cell++) {

			game_of_life_cell* cell_data = game_grid[*cell];

			outfile << cell_data->live_neighbour_count << endl;

		}

		// write each cells neighbour count
		outfile << "SCALARS neighbours int 1" << endl;
		outfile << "LOOKUP_TABLE default" << endl;
		for (vector<uint64_t>::const_iterator cell = cells.begin(); cell != cells.end(); cell++) {
			const vector<uint64_t>* neighbours = game_grid.get_neighbours(*cell);
			outfile << neighbours->size() << endl;
		}

		// write each cells process
		outfile << "SCALARS process int 1" << endl;
		outfile << "LOOKUP_TABLE default" << endl;
		for (vector<uint64_t>::const_iterator cell = cells.begin(); cell != cells.end(); cell++) {
			outfile << comm.rank() << endl;
		}

		// write each cells id
		outfile << "SCALARS id int 1" << endl;
		outfile << "LOOKUP_TABLE default" << endl;
		for (vector<uint64_t>::const_iterator cell = cells.begin(); cell != cells.end(); cell++) {
			outfile << *cell << endl;
		}
		outfile.close();


		// get the neighbour counts of every cell, starting with the cells whose neighbour data doesn't come from other processes
		vector<uint64_t> cells_with_local_neighbours = game_grid.get_cells_with_local_neighbours();
		for (vector<uint64_t>::const_iterator cell = cells_with_local_neighbours.begin(); cell != cells_with_local_neighbours.end(); cell++) {

			game_of_life_cell* cell_data = game_grid[*cell];

			cell_data->live_neighbour_count = 0;
			const vector<uint64_t>* neighbours = game_grid.get_neighbours(*cell);
			if (neighbours == NULL) {
				cout << "Process " << comm.rank() << ": neighbour list for cell " << *cell << " not available" << endl;
				exit(EXIT_FAILURE);
			}

			for (vector<uint64_t>::const_iterator neighbour = neighbours->begin(); neighbour != neighbours->end(); neighbour++) {
				if (*neighbour == 0) {
					continue;
				}

				game_of_life_cell* neighbour_data = game_grid[*neighbour];
				if (neighbour_data == NULL) {
					cout << "Process " << comm.rank() << ": neighbour " << *neighbour << " data for cell " << *cell << " not available" << endl;
					exit(EXIT_FAILURE);
				}
				if (neighbour_data->is_alive) {
					cell_data->live_neighbour_count++;
				}
			}
		}

		vector<uint64_t> cells_with_remote_neighbour = game_grid.get_cells_with_remote_neighbour();
		for (vector<uint64_t>::const_iterator cell = cells_with_remote_neighbour.begin(); cell != cells_with_remote_neighbour.end(); cell++) {

			game_of_life_cell* cell_data = game_grid[*cell];

			cell_data->live_neighbour_count = 0;
			const vector<uint64_t>* neighbours = game_grid.get_neighbours(*cell);
			if (neighbours == NULL) {
				cout << "Process " << comm.rank() << ": neighbour list for cell " << *cell << " not available" << endl;
				exit(EXIT_FAILURE);
			}

			for (vector<uint64_t>::const_iterator neighbour = neighbours->begin(); neighbour != neighbours->end(); neighbour++) {
				if (*neighbour == 0) {
					continue;
				}

				game_of_life_cell* neighbour_data = game_grid[*neighbour];
				if (neighbour_data == NULL) {
					cout << "Process " << comm.rank() << ": neighbour " << *neighbour << " data for cell " << *cell << " not available" << endl;
					exit(EXIT_FAILURE);
				}
				if (neighbour_data->is_alive) {
					cell_data->live_neighbour_count++;
				}
			}
		}

		// calculate the next turn
		for (vector<uint64_t>::const_iterator cell = cells_with_local_neighbours.begin(); cell != cells_with_local_neighbours.end(); cell++) {

			game_of_life_cell* cell_data = game_grid[*cell];

			if (cell_data->live_neighbour_count == 3) {
				cell_data->is_alive = true;
			} else if (cell_data->live_neighbour_count != 2) {
				cell_data->is_alive = false;
			}
		}
		for (vector<uint64_t>::const_iterator cell = cells_with_remote_neighbour.begin(); cell != cells_with_remote_neighbour.end(); cell++) {

			game_of_life_cell* cell_data = game_grid[*cell];

			if (cell_data->live_neighbour_count == 3) {
				cell_data->is_alive = true;
			} else if (cell_data->live_neighbour_count != 2) {
				cell_data->is_alive = false;
			}
		}

	}

	if (comm.rank() == 0) {
		visit_file.close();
		cout << endl;
	}

	return EXIT_SUCCESS;
}
