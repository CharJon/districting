// source file for single and multi commodity flow formulations
#include <unordered_map>
#include <vector>

#include "gurobi_c++.h"

#include "districting/graph.hpp"
#include "districting/models.hpp"


void build_shir(GRBModel* model, hess_params& p, graph* g)
{
  int n = g->nr_nodes;

  vector<int> centers;
  for (int i = 0; i < n; ++i)
    if (!p.F0[i][i])
      centers.push_back(i);

  int c = centers.size();

  // step 1 : hash edge (i,j) to i*n+j = h1
  // step 2 : hash every h1 to a number, resulting in exactly |E| variables

  std::unordered_map<int, int> hash_edges;
  int cur = 0;
  for (int i = 0; i < n; ++i)
    for (int j : g->nb(i))
      hash_edges.insert(std::make_pair(i*n + j, cur++));

  // get number of edges
  int nr_edges = hash_edges.size();

  // add flow variables f[v][i,j]
  GRBVar**f = new GRBVar*[c]; // commodity type, v
  for (int v = 0; v < c; ++v)
    f[v] = model->addVars(nr_edges, GRB_CONTINUOUS); // the edge

  // add constraint (b)
  for (int v = 0; v < c; ++v)
  {
    int j = centers[v];
    for (int i = 0; i < n; ++i)
    {
      if (i == j) continue;
      GRBLinExpr expr = 0;
      for (int nb_i : g->nb(i))
      {
        expr += f[v][hash_edges[n*nb_i + i]]; // in d^- : edge (nb_i -- i)
        expr -= f[v][hash_edges[n*i + nb_i]]; // in d^+ : edge (i -- nb_i)
      }
      model->addConstr(expr == X(i, j));
    }
  }

  // add constraint (c)
  for (int v = 0; v < c; ++v)
  {
    int j = centers[v];
    for (int i = 0; i < n; ++i)
    {
      if (i == j) continue;
      GRBLinExpr expr = 0;
      for (int nb_i : g->nb(i))
        expr += f[v][hash_edges[n*nb_i + i]]; // in d^- : edge (nb_i -- i)
      model->addConstr(expr <= (n - 1) * X(i, j));
    }
  }

  // add constraint (d) -- actually just fix individual vars to zero
  for (int v = 0; v < c; ++v)
  {
    int j = centers[v];
    for (int i : g->nb(j))
      f[v][hash_edges[n*i + j]].set(GRB_DoubleAttr_UB, 0.); // in d^+ : edge (nb_j -- j)
  }
}

void build_mcf(GRBModel* model, hess_params& p, graph* g)
{
    int n = g->nr_nodes;

    // hash edges similarly to mcf1
    std::unordered_map<int, int> hash_edges;
    int cur = 0;
    for (int i = 0; i < n; ++i)
        for (int j : g->nb(i))
            hash_edges.insert(std::make_pair(i*n + j, cur++));

    // get number of edges
    int nr_edges = hash_edges.size();

    GRBVar ***f = new GRBVar**[n]; // f[ b ][ (i,j) ][ a ]
    for (int i = 0; i < n; ++i)
    {
        f[i] = new GRBVar*[nr_edges];
        for (int j = 0; j < nr_edges; ++j)
            f[i][j] = model->addVars(n - g->nb(i).size() - 1, GRB_CONTINUOUS); // V = { i } u N(i) u (V \ N[i])
    }

    // preprocess V \ N[i] sets
    std::vector<std::vector<int>> non_nbs(n);

    for (int i = 0; i < n; ++i)
    {
        std::vector<bool> nb(n, false);
        nb[i] = true;
        for (int j : g->nb(i))
            nb[j] = true;
        nb.flip();
        for (int j = 0; j < n; ++j)
            if (nb[j])
                non_nbs[i].push_back(j);

        // check
        if (non_nbs[i].size() != n - 1 - g->nb(i).size())
            throw "Internal Error : non nb size for mcf2";
    }

    // add constraint (b)
    for (int b = 0; b < n; ++b)
        for (int a_i = 0; a_i < n - 1 - g->nb(b).size(); ++a_i)
        {
            GRBLinExpr expr = 0;
            for (int j : g->nb(b))
            {
                expr += f[b][hash_edges[b*n + j]][a_i]; // b -- j in d^+(b)
                expr -= f[b][hash_edges[j*n + b]][a_i]; // j -- b in d^-(b)
            }
            model->addConstr(expr == X(non_nbs[b][a_i],b));
        }

    // add constraint (c)
    for (int b = 0; b < n; ++b)
        for (int a_i = 0; a_i < n - 1 - g->nb(b).size(); ++a_i)
            for (int i = 0; i < n; ++i)
            {
                if (i == non_nbs[b][a_i] || i == b) continue;
                GRBLinExpr expr = 0;
                for (int j : g->nb(i))
                {
                    expr += f[b][hash_edges[i*n + j]][a_i];
                    expr -= f[b][hash_edges[j*n + i]][a_i];
                }
                model->addConstr(expr == 0);
            }

    // add constraint (d) -- actually just fix each var UB to zero
    for (int b = 0; b < n; ++b)
        for (int a_i = 0; a_i < n - 1 - g->nb(b).size(); ++a_i)
            for (int j : g->nb(b))
                f[b][hash_edges[j*n + b]][a_i].set(GRB_DoubleAttr_UB, 0.); // j -- b

    // add constraint (19e)
    for (int b = 0; b < n; ++b)
        for (int a_i = 0; a_i < n - 1 - g->nb(b).size(); ++a_i)
            for (int j = 0; j < n; ++j)
            {
                if (j == b) continue;
                GRBLinExpr expr = 0;
                for (int i : g->nb(j))
                    expr += f[b][hash_edges[i*n + j]][a_i]; // i -- j
                model->addConstr(expr <= X(j,b));
            }
}
