module RoboCompDSR
{
    dictionary<string, string> Attribs;
    struct EdgeAttribs
	{ 
		string label;
		int from;
        int to;
		Attribs attrs; 
	};
    dictionary<int, EdgeAttribs> FanOut;
    struct Content
    { 
        string type;
        int id;
        Attribs attrs;
        FanOut fano;
    };
    
    // Topic for graph  updates
    dictionary<int, Content> DSRGraph;

    // Topic for full graph requests
    struct GraphRequest
    {
        string from;
    };
}
