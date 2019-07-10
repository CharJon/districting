// Copyright Eugene Lykhovyd, 2017-2018.
#include "gurobi_c++.h"
#include "graph.h"
#include "io.h"
#include "models.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>

#ifndef sign
#define sign(x) (((x)>0)?1:((x)==0)?0:(-1))
#endif

using namespace std;
//extern const char* gitversion;

#define DO_BATCH_OUTPUT

int main(int argc, char *argv[])
{
	ios::sync_with_stdio(1);
	//printf("Districting, build %s\n", gitversion);
	if (argc < 8) {
		printf("Usage: %s <dimacs> <distance> <population> <L|auto> <U|auto> <k> <model> [ralg hot start]\n\
  Available models:\n\
  \thess\t\tHess model\n\
  \tshir\t\tHess model with SHIR\n\
  \tmcf\t\tHess model with MCF\n\
  \tcut\t\tHess model with CUT\n\
  \tlcut\t\tHess model with LCUT\n", argv[0]);
		return 0;
	}
	// parse command line arguments
	char* dimacs_fname = argv[1];
	char* distance_fname = argv[2];
	char* population_fname = argv[3];
	int L = read_auto_int(argv[4], 0);
	int U = read_auto_int(argv[5], 0);
	bool ralg_hot_start = argc > 8;
	char* ralg_hot_start_fname = ralg_hot_start ? argv[8] : nullptr;

	// read inputs
	graph* g = 0;
	vector<vector<int> > dist;
	vector<int> population;
	if (read_input_data(dimacs_fname, distance_fname, population_fname, g, dist, population))
		return 1; // failure

	cerr << dimacs_fname << " " << g->nr_nodes << " ";

	int k = read_auto_int(argv[6], g->get_k());

	if (L == 0 && U == 0)
		calculate_UL(population, k, &L, &U);

	printf("Model input: L = %d, U = %d, k = %d\n", L, U, k);

	g->connect(dist);

	// check connectivity
	if (!g->is_connected())
	{
		printf("Problem is infeasible (not connected!)\n");
#ifdef DO_BATCH_OUTPUT
		printf("qwerky567: %s, disconnected\n", dimacs_fname);
#endif
		return 1;
	}

	if (g->nr_nodes <= 0)
	{
		fprintf(stderr, "empty graph\n");
		return 1;
	}

	if (dist.size() != g->nr_nodes || population.size() != g->nr_nodes)
	{
		fprintf(stderr, "dist/population size != n, expected %d\n", g->nr_nodes);
		return 1;
	}

	long total_pop = 0L;
	for (int p : population)
		total_pop += p;
	printf("Model input: total population = %ld\n", total_pop);

	string arg_model = argv[7];

	//apply the merging preprocess and get the clusters
	vector<vector<int>> clusters;
  bool do_heur_cuts = false;
	if (arg_model == "shir" || arg_model == "mcf" || arg_model == "cut")
	{
    do_heur_cuts = true;
		printf("Preprocessing the graph...\n");
		vector<int> new_population;
		new_population = population;
		clusters = preprocess(g, new_population, L, U, population);
	}

	auto start = chrono::steady_clock::now();

	// set objective function coefficients
	vector<vector<double>> w(g->nr_nodes, vector<double>(g->nr_nodes)); // this is the weight matrix in the objective function
	for (int i = 0; i < g->nr_nodes; i++)
		for (int j = 0; j < g->nr_nodes; j++)
			w[i][j] = get_objective_coefficient(dist, population, i, j);

	// apply Lagrangian 
	vector< vector<double> > LB0(g->nr_nodes, vector<double>(g->nr_nodes,-INFINITY)); // LB0[i][j] is a lower bound on problem objective if we fix x[i][j] = 0
	vector< vector<double> > LB1(g->nr_nodes, vector<double>(g->nr_nodes,-INFINITY)); // LB1[i][j] is a lower bound on problem objective if we fix x[i][j] = 1
	vector<int> lagrangianCenters(k, -1);									// the centers coming from the best lagrangian inner problem
	auto lagrange_start = chrono::steady_clock::now();
	double LB = solveLagrangian(g, w, population, L, U, k, LB0, LB1, lagrangianCenters, ralg_hot_start, ralg_hot_start_fname);// lower bound on problem objective, coming from lagrangian
	chrono::duration<double> lagrange_duration = chrono::steady_clock::now() - lagrange_start;
	cerr << LB << " " << lagrange_duration.count() << " ";

	// run a heuristic
	double UB = INFINITY;
	int maxIterations = 10;		// 10 iterations is often sufficient
	auto heuristic_start = chrono::steady_clock::now();
	vector<int> heuristicSolution = HessHeuristic(g, w, population, L, U, k, UB, maxIterations, false);
	chrono::duration<double> heuristic_duration = chrono::steady_clock::now() - heuristic_start;
	cerr << UB << " " << heuristic_duration.count() << " ";
	cout << "Best solution after " << maxIterations << " of HessHeuristic is = " << UB << endl;

	// run local search
	auto LS_start = chrono::steady_clock::now();
	LocalSearch(g, w, population, L, U, k, heuristicSolution, arg_model, UB); // , F0);
	chrono::duration<double> LS_duration = chrono::steady_clock::now() - LS_start;
	cerr << UB << " " << LS_duration.count() << " ";
	cout << "Best solution after local search is = " << UB << endl;

	// determine which variables can be fixed
	vector<vector<bool>> F0(g->nr_nodes, vector<bool>(g->nr_nodes, false)); // define matrix F_0
	vector<vector<bool>> F1(g->nr_nodes, vector<bool>(g->nr_nodes, false)); // define matrix F_1
	for (int i = 0; i < g->nr_nodes; ++i)
	{
		for (int j = 0; j < g->nr_nodes; ++j)
		{
			if (LB0[i][j] > UB + 0.00001) F1[i][j] = true;
			if (LB1[i][j] > UB + 0.00001) F0[i][j] = true;
		}
	}

	// report the number of fixings
	int numFixedZero = 0;
	int numFixedOne = 0;
	int numUnfixed = 0;
	int numCentersLeft = 0;
	for (int i = 0; i < g->nr_nodes; ++i)
	{
		if (!F0[i][i]) numCentersLeft++;
		for (int j = 0; j < g->nr_nodes; ++j)
		{
			if (F0[i][j]) numFixedZero++;
			else if (F1[i][j]) numFixedOne++;
			else numUnfixed++;
		}
	}
	cout << endl;
	cout << "Number of variables fixed to zero = " << numFixedZero << endl;
	cout << "Number of variables fixed to one  = " << numFixedOne << endl;
	cout << "Number of variables not fixed     = " << numUnfixed << endl;
	cout << "Number of centers left            = " << numCentersLeft << endl;
	cout << "Percentage of vars fixed = " << (double)(g->nr_nodes*g->nr_nodes - numUnfixed) / (g->nr_nodes*g->nr_nodes) << endl;


	try 
	{
		// initialize environment and create an empty model
		GRBEnv env = GRBEnv();
		GRBModel model = GRBModel(env);

		// get incumbent solution using centers from lagrangian
    hess_params p;
    p = build_hess(&model, g, w, population, L, U, k, F0, F1);

		// push GUROBI to branch over clusterheads
		for (int i = 0; i < g->nr_nodes; ++i)
			if (IS_X(i, i))
				X_V(i,i).set(GRB_IntAttr_BranchPriority, 1);

		HessCallback* cb = 0;

		//	strengthen_hess(&model, x, g, clusters);

		if (arg_model == "shir")
			build_shir(&model, p, g);
		else if (arg_model == "mcf")
			build_mcf(&model, p, g);
		else if (arg_model == "cut")
			cb = build_cut(&model, p, g, population);
    else if (arg_model == "lcut")
      cb = build_lcut(&model, p, g, population);
		else if (arg_model != "hess") {
			fprintf(stderr, "ERROR: Unknown model : %s\n", arg_model.c_str());
			exit(1);
		}

		//TODO change user-interactive?
		model.set(GRB_DoubleParam_TimeLimit, 3600.); // 1 hour
		//model.set(GRB_IntParam_Threads, 10);
		model.set(GRB_DoubleParam_NodefileStart, 10); // 10 GB
		model.set(GRB_IntParam_Method, 3);
		model.set(GRB_DoubleParam_MIPGap, 0);

		// provide IP warm start 
		for (int i = 0; i < g->nr_nodes; ++i)
		{
			for (int j = 0; j < g->nr_nodes; ++j)
				if(IS_X(i,j))
					X_V(i,j).set(GRB_DoubleAttr_Start, 0);

			if(IS_X(i,heuristicSolution[i]))
				X_V(i, heuristicSolution[i]).set(GRB_DoubleAttr_Start, 1);
		}

		//optimize the model
		auto IP_start = chrono::steady_clock::now();
		model.optimize();
		chrono::duration<double> IP_duration = chrono::steady_clock::now() - IP_start;
		cerr << IP_duration.count() << " ";

		chrono::duration<double> duration = chrono::steady_clock::now() - start;
		printf("Time elapsed: %lf seconds\n", duration.count()); // TODO use gurobi Runtime model attr
		if (cb)
		{
			printf("Number of callbacks: %d\n", cb->numCallbacks);
			printf("Time in callbacks: %lf seconds\n", cb->callbackTime);
			printf("Number of lazy constraints generated: %d\n", cb->numLazyCuts);
			delete cb;
		}
		cerr << duration.count() << " ";

#ifdef DO_BATCH_OUTPUT

		printf("qwerky567: %s, %d, %d, %d, %d, %.2lf", dimacs_fname, k, g->nr_nodes, L, U, duration.count());

		// output overtly
		int max_pv = population[0];
		for (int pv : population)
			max_pv = max(max_pv, pv);


		printf(",%.2lf", static_cast<double>(max_pv) / static_cast<double>(U));

		// will remain temporary for script run
		if (model.get(GRB_IntAttr_Status) == 3) // infeasible
			printf(",infeasible,,");
		else {

			double objval = model.get(GRB_DoubleAttr_ObjVal);
			double mipgap = model.get(GRB_DoubleAttr_MIPGap)*100.;
			double objbound = model.get(GRB_DoubleAttr_ObjBound);

			cerr << objbound << " " << objval << endl;

			// no incumbent solution was found, these values do no make sense
			if (model.get(GRB_IntAttr_SolCount) == 0)
			{
				mipgap = 100.;
				objval = 0.;
			}

			printf(", %.2lf, %.2lf, %.2lf", objval, mipgap, objbound);
		}

		long nodecount = static_cast<long>(model.get(GRB_DoubleAttr_NodeCount));

		int num_callbacks = 0;
		double time_callbacks = 0.;
		int num_lazy = 0;
		if (cb) {
			num_callbacks = cb->numCallbacks;
			time_callbacks = cb->callbackTime;
			num_lazy = cb->numLazyCuts;
		}
		// state, k n l u time obj mipgap objbound nodes callback x3
		printf(", %ld, %d, %.2lf, %d\n", nodecount, num_callbacks, time_callbacks, num_lazy);

		// will remain temporary for script run
		//if(model.get(GRB_IntAttr_SolCount) > 0)
		// printf("qwerky567: sol: L = %.4lf, U = %.4lf\n", x[g->nr_nodes][0].get(GRB_DoubleAttr_X), x[g->nr_nodes][1].get(GRB_DoubleAttr_X));
		//else
		// printf("qwerly567: sol: no incumbent solution found!\n");
#endif

		if (model.get(GRB_IntAttr_Status) != 3) {
			vector<int> sol;
			translate_solution(p, sol, g->nr_nodes);
			string fn = string(dimacs_fname);
			string soln_fn = fn.substr(0, 2) + "_" + arg_model + ".sol";
			int len = soln_fn.length() + 1;
			char* char_array = new char[len];
			strcpy(char_array, soln_fn.c_str());
			printf_solution(sol, char_array);
		}

	}
	catch (GRBException e) {
		cout << "Error code = " << e.getErrorCode() << endl;
		cout << e.getMessage() << endl;
	}
	catch (const char* msg) {
		cout << "Exception with message : " << msg << endl;
	}
	catch (...) {
		cout << "Exception during optimization" << endl;
	}


	delete g;
	return 0;
}
