/**
 * 
 */
package org.gprom.jdbc.testing;


import java.util.ArrayList;
import java.util.Arrays;
import java.util.Map;

import org.gprom.jdbc.pawd.Edge;
import org.gprom.jdbc.pawd.Node;
import org.gprom.jdbc.pawd.VersionEdge;
import org.gprom.jdbc.pawd.VersionGraph;
import org.gprom.jdbc.pawd.VersionGraphManager;
import org.gprom.jdbc.pawd.VersionGraphStore.Operation;
import org.gprom.jdbc.pawd.VersionGraphStore.Operation.Materialization;
import org.gprom.jdbc.pawd.VersionGraphStore.Operation.OpType;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

/**
 * @author Amer
 *
 */
public class MaterializationPlanGeneration {
	VersionGraph VG1;
	Node T;
	/**
	 * @throws java.lang.Exception
	 */
	@Before
	public void setUp() throws Exception {
		Node R = new Node(false, "R");
		Node S = new Node(false, "S");
		T = new Node(false,"T");
		Node Rprime = new Node(false, "R'");
		//construct different arraylist for Edge construction
		ArrayList<Node> NodeSetAll = new ArrayList<>(Arrays.asList(R,S,T,Rprime));
		//construct sample operations for edge creation
		Operation op1 = new Operation("SELECT a, b * 2 AS b FROM $$1$$",OpType.Query);
		Operation op2 = new Operation("SELECT sum(a), b FROM $$1$$ GROUP BY b",OpType.Update);
		//sample set of edges
		Edge edge1 = new Edge(R,S,op1);
		Edge edge2 = new Edge(S,T,op2);
		//construct Arraylist of EDGES
		ArrayList<Edge> EdgeSetAll = new ArrayList<>( Arrays.asList(edge1,edge2));//,edge3,edge4,edge5,edge6));
		//construct the VersionEdges
		VersionEdge VE1 = new VersionEdge(R,Rprime);
		ArrayList<VersionEdge> VersionEdgeSetAll = new ArrayList<>( Arrays.asList(VE1));
		//create a version graph
		VG1 = new VersionGraph(NodeSetAll, EdgeSetAll,VersionEdgeSetAll,null);
	}

	/**
	 * @throws java.lang.Exception
	 */
	@After
	public void tearDown() throws Exception {
	}

	@Test
	public void test() {
		VersionGraphManager myManager = new VersionGraphManager();
		for(Map<Node,Materialization> plan: myManager.genMaterializationPlans(VG1, T)){
			for (Map.Entry<Node,Materialization> entry : plan.entrySet()) {
				  String key = entry.getKey().getDescription();
				  String value = entry.getValue().toString();
				  System.out.print("KEY "+ key );
				  System.out.print(" Value "+ value);
				  System.out.print("	");
			}
			System.out.print("\n");
		}
	}

}
