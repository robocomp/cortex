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
    dictionary<int, Content> DSRGraph;
    
    struct DSRPackage
    {
        DSRGraph graph;
        Content node;
    }
}
