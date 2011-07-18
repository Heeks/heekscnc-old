// Drilling.cpp
/*
 * Copyright (c) 2009, Dan Heeks, Perttu Ahola
 * This program is released under the BSD license. See the file COPYING for
 * details.
 */

#include "stdafx.h"
#include "Drilling.h"
#include "CNCConfig.h"
#include "ProgramCanvas.h"
#include "interface/HeeksObj.h"
#include "interface/PropertyInt.h"
#include "interface/PropertyDouble.h"
#include "interface/PropertyLength.h"
#include "interface/PropertyChoice.h"
#include "interface/PropertyString.h"
#include "tinyxml/tinyxml.h"
#include "Operations.h"
#include "CTool.h"
#include "Profile.h"
#include "Fixture.h"
#include "CNCPoint.h"
#include "MachineState.h"
#include "Program.h"
#include "CounterBore.h"

#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

extern CHeeksCADInterface* heeksCAD;


void CDrillingParams::set_initial_values( const double depth, const int tool_number )
{
	CNCConfig config(ConfigScope());

	config.Read(_T("m_standoff"), &m_standoff, (25.4 / 4));	// Quarter of an inch
	config.Read(_T("m_dwell"), &m_dwell, 1);
	config.Read(_T("m_depth"), &m_depth, 25.4);		// One inch
	config.Read(_T("m_peck_depth"), &m_peck_depth, (25.4 / 10));	// One tenth of an inch
	config.Read(_T("m_sort_drilling_locations"), &m_sort_drilling_locations, 1);
	config.Read(_T("m_retract_mode"), &m_retract_mode, 0);
	config.Read(_T("m_spindle_mode"), &m_spindle_mode, 0);
	config.Read(_T("m_clearance_height"), &m_clearance_height, 25.4);		// One inch

	if (depth > 0)
	{
		// We've found the depth we want used.  Assign it now.
		m_depth = depth;
	} // End if - then

	// The following is taken from the 'rule of thumb' document that Stanley Dornfeld put
	// together for drilling feeds and speeds.  It includes a statement something like;
	// "We most always peck every one half drill diameter in depth after the first peck of
	// three diameters".  From this, we will take his advice and set a default peck depth
	// that is half the drill's diameter.
	//
	// NOTE: If the peck depth is zero (or less) then the operator may have manually chosen
	// to not peck.  In this case, don't add a positive peck depth - which would force
	// a pecking cycle rather than another drilling cycle.
	if ((tool_number > 0) && (m_peck_depth > 0.0))
	{
		CTool *pTool = CTool::Find( tool_number );
		if (pTool != NULL)
		{
			m_peck_depth = pTool->m_params.m_diameter / 2.0;
		}
	}

}

void CDrillingParams::write_values_to_config()
{
	// We always want to store the parameters in mm and convert them back later on.

	CNCConfig config(ConfigScope());

	// These values are in mm.
	config.Write(_T("m_standoff"), m_standoff);
	config.Write(_T("m_dwell"), m_dwell);
	config.Write(_T("m_depth"), m_depth);
	config.Write(_T("m_peck_depth"), m_peck_depth);
	config.Write(_T("m_sort_drilling_locations"), m_sort_drilling_locations);
	config.Write(_T("m_retract_mode"), m_retract_mode);
	config.Write(_T("m_spindle_mode"), m_spindle_mode);
	config.Write(_T("m_clearance_height"), m_clearance_height);

}


static void on_set_spindle_mode(int value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_spindle_mode = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_retract_mode(int value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_retract_mode = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_standoff(double value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_standoff = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_dwell(double value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_dwell = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_depth(double value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_depth = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_peck_depth(double value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_peck_depth = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_sort_drilling_locations(int value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.m_sort_drilling_locations = value;
	((CDrilling*)object)->m_params.write_values_to_config();
}

static void on_set_clearance_height(double value, HeeksObj* object)
{
	((CDrilling*)object)->m_params.ClearanceHeight( value );
	((CDrilling*)object)->m_params.write_values_to_config();
}

void CDrillingParams::GetProperties(CDrilling* parent, std::list<Property *> *list)
{
	list->push_back(new PropertyLength(_("standoff"), m_standoff, parent, on_set_standoff));

	switch(theApp.m_program->m_clearance_source)
	{
	case CProgram::eClearanceDefinedByFixture:
		list->push_back(new PropertyString(_("clearance height"), _("Defined in fixture definition"), NULL, NULL));
		break;

	case CProgram::eClearanceDefinedByMachine:
		list->push_back(new PropertyString(_("clearance height"), _("Defined in Program properties for whole machine"), NULL, NULL));
		break;

	case CProgram::eClearanceDefinedByOperation:
	default:
		list->push_back(new PropertyLength(_("clearance height"), m_clearance_height, parent, on_set_clearance_height));
	} // End switch

	list->push_back(new PropertyDouble(_("dwell"), m_dwell, parent, on_set_dwell));
	list->push_back(new PropertyLength(_("depth"), m_depth, parent, on_set_depth));
	list->push_back(new PropertyLength(_("peck_depth"), m_peck_depth, parent, on_set_peck_depth));
	{ // Begin choice scope
		std::list< wxString > choices;

		choices.push_back(_("Rapid retract"));	// Must be 'false' (0)
		choices.push_back(_("Feed retract"));	// Must be 'true' (non-zero)

		int choice = int(m_retract_mode);
		list->push_back(new PropertyChoice(_("retract_mode"), choices, choice, parent, on_set_retract_mode));
	} // End choice scope
	{ // Begin choice scope
		std::list< wxString > choices;

		choices.push_back(_("Keep running"));	// Must be 'false' (0)
		choices.push_back(_("Stop at bottom"));	// Must be 'true' (non-zero)

		int choice = int(m_spindle_mode);
		list->push_back(new PropertyChoice(_("spindle_mode"), choices, choice, parent, on_set_spindle_mode));
	} // End choice scope
	{ // Begin choice scope
		std::list< wxString > choices;

		choices.push_back(_("Respect existing order"));	// Must be 'false' (0)
		choices.push_back(_("True"));			// Must be 'true' (non-zero)

		int choice = int(m_sort_drilling_locations);
		list->push_back(new PropertyChoice(_("sort_drilling_locations"), choices, choice, parent, on_set_sort_drilling_locations));
	} // End choice scope

}

void CDrillingParams::WriteXMLAttributes(TiXmlNode *root)
{
	TiXmlElement * element;
	element = heeksCAD->NewXMLElement( "params" );
	heeksCAD->LinkXMLEndChild( root,  element );

	element->SetDoubleAttribute( "standoff", m_standoff);
	element->SetDoubleAttribute( "dwell", m_dwell);
	element->SetDoubleAttribute( "depth", m_depth);
	element->SetDoubleAttribute( "peck_depth", m_peck_depth);

	element->SetAttribute( "sort_drilling_locations", m_sort_drilling_locations);
	element->SetAttribute( "retract_mode", m_retract_mode);
	element->SetAttribute( "spindle_mode", m_spindle_mode);
	element->SetAttribute( "clearance_height", m_clearance_height);
}

void CDrillingParams::ReadParametersFromXMLElement(TiXmlElement* pElem)
{
	if (pElem->Attribute("standoff")) pElem->Attribute("standoff", &m_standoff);
	m_clearance_height = m_standoff;  // Default if the clearance_height parameter is not found.
	if (pElem->Attribute("dwell")) pElem->Attribute("dwell", &m_dwell);
	if (pElem->Attribute("depth")) pElem->Attribute("depth", &m_depth);
	if (pElem->Attribute("peck_depth")) pElem->Attribute("peck_depth", &m_peck_depth);
	if (pElem->Attribute("sort_drilling_locations")) pElem->Attribute("sort_drilling_locations", &m_sort_drilling_locations);
	if (pElem->Attribute("retract_mode")) pElem->Attribute("retract_mode", &m_retract_mode);
	if (pElem->Attribute("spindle_mode")) pElem->Attribute("spindle_mode", &m_spindle_mode);
	if (pElem->Attribute("clearance_height")) pElem->Attribute("clearance_height", &m_clearance_height);
}

const wxBitmap &CDrilling::GetIcon()
{
	if(!m_active)return GetInactiveIcon();
	static wxBitmap* icon = NULL;
	if(icon == NULL)icon = new wxBitmap(wxImage(theApp.GetResFolder() + _T("/icons/drilling.png")));
	return *icon;
}

/**
	This method is called when the CAD operator presses the Python button.  This method generates
	Python source code whose job will be to generate RS-274 GCode.  It's done in two steps so that
	the Python code can be configured to generate GCode suitable for various CNC interpreters.
 */
Python CDrilling::AppendTextToProgram( CMachineState *pMachineState )
{
	Python python;

	python << CSpeedOp::AppendTextToProgram( pMachineState );   // Set any private fixtures and change tools (if necessary)

	std::vector<CNCPoint> locations = CDrilling::FindAllLocations(this, pMachineState->Location(), m_params.m_sort_drilling_locations != 0, NULL);
	for (std::vector<CNCPoint>::const_iterator l_itLocation = locations.begin(); l_itLocation != locations.end(); l_itLocation++)
	{
#ifdef STABLE_OPS_ONLY
		gp_Pnt point = *l_itLocation;
#else
		gp_Pnt point = pMachineState->Fixture().Adjustment( *l_itLocation );
#endif

		python << _T("drill(")
			<< _T("x=") << point.X()/theApp.m_program->m_units << _T(", ")
			<< _T("y=") << point.Y()/theApp.m_program->m_units << _T(", ")
			<< _T("z=") << point.Z()/theApp.m_program->m_units << _T(", ")
			<< _T("depth=") << m_params.m_depth/theApp.m_program->m_units << _T(", ")
			<< _T("standoff=") << m_params.m_standoff/theApp.m_program->m_units << _T(", ")
			<< _T("dwell=") << m_params.m_dwell << _T(", ")
			<< _T("peck_depth=") << m_params.m_peck_depth/theApp.m_program->m_units << _T(", ")
			<< _T("retract_mode=") << m_params.m_retract_mode << _T(", ")
			<< _T("spindle_mode=") << m_params.m_spindle_mode << _T(", ")
			<< _T("clearance_height=") << m_params.ClearanceHeight()
			<< _T(")\n");
        pMachineState->Location(point); // Remember where we are.
	} // End for

	python << _T("end_canned_cycle()\n");

	return(python);
}


/**
	This routine generates a list of coordinates around the circumference of a circle.  It's just used
	to generate data suitable for OpenGL calls to paint a circle.  This graphics is transient but will
	help represent what the GCode will be doing when it's generated.
 */
std::list< CNCPoint > CDrilling::PointsAround(
		const CNCPoint & origin,
		const double radius,
		const unsigned int numPoints ) const
{
	std::list<CNCPoint> results;

	double alpha = 3.1415926 * 2 / numPoints;

	unsigned int i = 0;
	while( i++ < numPoints )
	{
		double theta = alpha * i;
		CNCPoint pointOnCircle( cos( theta ) * radius, sin( theta ) * radius, 0 );
		pointOnCircle += origin;
		results.push_back(pointOnCircle);
	} // End while

	return(results);

} // End PointsAround() routine


/**
	Generate a list of vertices that represent the hole that will be drilled.  Let it be a circle at the top, a
	spiral down the length and a countersunk base.

	This method is only called by the glCommands() method.  This means that the graphics is transient.

	TODO: Handle drilling in any rotational angle. At the moment it only handles drilling 'down' along the 'z' axis
 */

std::list< CNCPoint > CDrilling::DrillBitVertices( const CNCPoint & origin, const double radius, const double length ) const
{
	std::list<CNCPoint> top, spiral, bottom, countersink, result;

	double flutePitch = 5.0;	// 5mm of depth per spiral of the drill bit's flute.
	double countersinkDepth = -1 * radius * tan(31.0); // this is the depth of the countersink cone at the end of the drill bit. (for a typical 118 degree bevel)
	unsigned int numPoints = 20;	// number of points in one circle (360 degrees) i.e. how smooth do we want the graphics
	const double pi = 3.1415926;
	double alpha = 2 * pi / numPoints;

	// Get a circle at the top of the dill bit's path
	top = PointsAround( origin, radius, numPoints );
	top.push_back( *(top.begin()) );	// Close the circle

	double depthPerItteration;
	countersinkDepth = -1 * radius * tan(31.0);	// For a typical (118 degree bevel on the drill bit tip)

	unsigned int l_iNumItterations = numPoints * (length / flutePitch);
	depthPerItteration = (length - countersinkDepth) / l_iNumItterations;

	// Now generate the spirals.

	unsigned int i = 0;
	while( i++ < l_iNumItterations )
	{
		double theta = alpha * i;
		CNCPoint pointOnCircle( cos( theta ) * radius, sin( theta ) * radius, 0 );
		pointOnCircle += origin;

		// And spiral down as we go.
		pointOnCircle.SetZ( pointOnCircle.Z() - (depthPerItteration * i) );

		spiral.push_back(pointOnCircle);
	} // End while

	// And now the countersink at the bottom of the drill bit.
	i = 0;
	while( i++ < numPoints )
	{
		double theta = alpha * i;
		CNCPoint topEdge( cos( theta ) * radius, sin( theta ) * radius, 0 );

		// This is at the top edge of the countersink
		topEdge.SetX( topEdge.X() + origin.X() );
		topEdge.SetY( topEdge.Y() + origin.Y() );
		topEdge.SetZ( origin.Z() - (length - countersinkDepth) );
		spiral.push_back(topEdge);

		// And now at the very point of the countersink
		CNCPoint veryTip( origin );
		veryTip.SetZ( (origin.Z() - length) );

		spiral.push_back(veryTip);
		spiral.push_back(topEdge);
	} // End while

	std::copy( top.begin(), top.end(), std::inserter( result, result.begin() ) );
	std::copy( spiral.begin(), spiral.end(), std::inserter( result, result.end() ) );
	std::copy( countersink.begin(), countersink.end(), std::inserter( result, result.end() ) );

	return(result);

} // End DrillBitVertices() routine


/**
	This is the Graphics Library Commands (from the OpenGL set).  This method calls the OpenGL
	routines to paint the drill action in the graphics window.  The graphics is transient.

	Part of its job is to re-paint the elements that this CDrilling object refers to so that
	we know what CAD objects this CNC operation is referring to.
 */
void CDrilling::glCommands(bool select, bool marked, bool no_color)
{
	CSpeedOp::glCommands(select, marked, no_color);

	if(marked && !no_color)
	{
		double l_dHoleDiameter = 12.7;	// Default at half-inch (in mm)

		if (m_tool_number > 0)
		{
			HeeksObj* Tool = heeksCAD->GetIDObject( ToolType, m_tool_number );
			if (Tool != NULL)
			{
                		l_dHoleDiameter = ((CTool *) Tool)->m_params.m_diameter;
			} // End if - then
		} // End if - then

		std::vector<CNCPoint> locations = CDrilling::FindAllLocations(this);

		for (std::vector<CNCPoint>::const_iterator l_itLocation = locations.begin(); l_itLocation != locations.end(); l_itLocation++)
		{
			GLdouble start[3], end[3];

			start[0] = l_itLocation->X();
			start[1] = l_itLocation->Y();
			start[2] = l_itLocation->Z();

			end[0] = l_itLocation->X();
			end[1] = l_itLocation->Y();
			end[2] = l_itLocation->Z();

			end[2] -= m_params.m_depth;

			glBegin(GL_LINE_STRIP);
			glVertex3dv( start );
			glVertex3dv( end );
			glEnd();

			std::list< CNCPoint > pointsAroundCircle = DrillBitVertices( 	*l_itLocation,
												l_dHoleDiameter / 2,
												m_params.m_depth);

			glBegin(GL_LINE_STRIP);
			CNCPoint previous = *(pointsAroundCircle.begin());
			for (std::list< CNCPoint >::const_iterator l_itPoint = pointsAroundCircle.begin();
				l_itPoint != pointsAroundCircle.end();
				l_itPoint++)
			{

				glVertex3d( l_itPoint->X(), l_itPoint->Y(), l_itPoint->Z() );
			}
			glEnd();
		} // End for
	} // End if - then
}


void CDrilling::GetProperties(std::list<Property *> *list)
{
	m_params.GetProperties(this, list);
	CSpeedOp::GetProperties(list);
}

HeeksObj *CDrilling::MakeACopy(void)const
{
	return new CDrilling(*this);
}

void CDrilling::CopyFrom(const HeeksObj* object)
{
	if (object->GetType() == GetType())
	{
		operator=(*((CDrilling*)object));
	}
}

CDrilling::CDrilling(	const Symbols_t &symbols,
        const int tool_number,
        const double depth )
    : CSpeedOp(GetTypeString(), tool_number, DrillingType), m_symbols(symbols)
{
    m_params.set_initial_values(depth, tool_number);
    for (Symbols_t::iterator itSymbol = m_symbols.begin(); itSymbol != m_symbols.end(); itSymbol++)
    {
        HeeksObj *obj = heeksCAD->GetIDObject(itSymbol->first, itSymbol->second);
        if (obj != NULL)
        {
            Add(obj, NULL);
        }
    } // End for
    m_symbols.clear();	// We don't want to convert them twice.
}


CDrilling::CDrilling( const CDrilling & rhs ) : CSpeedOp( rhs )
{
	std::copy( rhs.m_symbols.begin(), rhs.m_symbols.end(), std::inserter( m_symbols, m_symbols.begin() ));
    m_params = rhs.m_params;
}

CDrilling & CDrilling::operator= ( const CDrilling & rhs )
{
	if (this != &rhs)
	{
		CSpeedOp::operator=(rhs);
		m_symbols.clear();
		std::copy( rhs.m_symbols.begin(), rhs.m_symbols.end(), std::inserter( m_symbols, m_symbols.begin() ));

		m_params = rhs.m_params;
	}

	return(*this);
}

bool CDrilling::CanAddTo(HeeksObj* owner)
{
    if (owner == NULL) return(false);

	int type = owner->GetType();

	if (type == OperationsType) return(true);
	if (type == CounterBoreType) return(true);

	return(false);
}

bool CDrilling::CanAdd(HeeksObj* object)
{
	return(CDrilling::ValidType(object->GetType()));
}

void CDrilling::WriteXML(TiXmlNode *root)
{
	TiXmlElement * element = heeksCAD->NewXMLElement( "Drilling" );
	heeksCAD->LinkXMLEndChild( root,  element );
	m_params.WriteXMLAttributes(element);

	TiXmlElement * symbols;
	symbols = heeksCAD->NewXMLElement( "symbols" );
	heeksCAD->LinkXMLEndChild( element, symbols );

	WriteBaseXML(element);
}

// static member function
HeeksObj* CDrilling::ReadFromXMLElement(TiXmlElement* element)
{
	CDrilling* new_object = new CDrilling;

	std::list<TiXmlElement *> elements_to_remove;

	// read point and circle ids
	for(TiXmlElement* pElem = heeksCAD->FirstXMLChildElement( element ) ; pElem; pElem = pElem->NextSiblingElement())
	{
		std::string name(pElem->Value());
		if(name == "params"){
			new_object->m_params.ReadParametersFromXMLElement(pElem);
			elements_to_remove.push_back(pElem);
		}
		else if(name == "symbols"){
			for(TiXmlElement* child = heeksCAD->FirstXMLChildElement( pElem ) ; child; child = child->NextSiblingElement())
			{
				if (child->Attribute("type") && child->Attribute("id"))
				{
					new_object->AddSymbol( atoi(child->Attribute("type")), atoi(child->Attribute("id")) );

					// We need to convert these type/id pairs into HeeksObj pointers but we want them to
					// come from the right source.  If we're importing data then they need to come from the
					// data we're importing.  If we're updating the main model then the main tree
					// will do.  We don't want to just use heeksCAD->GetIDObject() here as it will always
					// look in the main tree.  Perhaps we can force a recursive 'ReloadPointers()' call
					// so that these values are reset when necessary.  Eventually we will be storing the
					// child elements as real XML elements rather than just references.  Until time passes
					// a little longer, we need to support this type/id version.  Otherwise old HeeksCNC files
					// won't read in correctly.
				}
			} // End for
			elements_to_remove.push_back(pElem);
		} // End if
	}

	for (std::list<TiXmlElement*>::iterator itElem = elements_to_remove.begin(); itElem != elements_to_remove.end(); itElem++)
	{
		heeksCAD->RemoveXMLChild( element, *itElem);
	}

	new_object->ReadBaseXML(element);

	return new_object;
}


/**
	The old version of the CDrilling object stored references to graphics as type/id pairs
	that get read into the m_symbols list.  The new version stores these graphics references
	as child elements (based on ObjList).  If we read in an old-format file then the m_symbols
	list will have data in it for which we don't have children.  This routine converts
	these type/id pairs into the HeeksObj pointers as children.
 */
void CDrilling::ReloadPointers()
{
	for (Symbols_t::iterator symbol = m_symbols.begin(); symbol != m_symbols.end(); symbol++)
	{
		HeeksObj *object = heeksCAD->GetIDObject( symbol->first, symbol->second );
		if (object != NULL)
		{
			Add( object, NULL );
		}
	}

	m_symbols.clear();	// We don't want to convert them twice.

	CSpeedOp::ReloadPointers();
}



/**
 * 	This method looks through the symbols in the list.  If they're PointType objects
 * 	then the object's location is added to the result set.  If it's a circle object
 * 	that doesn't intersect any other element (selected) then add its centre to
 * 	the result set.  Finally, find the intersections of all of these elements and
 * 	add the intersection points to the result vector.
 */
/* static */ std::vector<CNCPoint> CDrilling::FindAllLocations(
                    ObjList *parent,
                    const CNCPoint starting_location, // = CNCPoint(0.0, 0.0, 0.0)
                    const bool sort_locations, // = false
                    std::list<int> *pToolNumbersReferenced /* = NULL */ )
{
	std::vector<CNCPoint> locations;
	parent->ReloadPointers();   // Make sure our integer lists have been converted into children first.

	// Look to find all intersections between all selected objects.  At all these locations, create
	// a drilling cycle.

	std::list<HeeksObj *> lhs_children;
	std::list<HeeksObj *> rhs_children;
	for (HeeksObj *lhsPtr = parent->GetFirstChild(); lhsPtr != NULL; lhsPtr = parent->GetNextChild())
	{
	    lhs_children.push_back( lhsPtr );
	    rhs_children.push_back( lhsPtr );
	}

	for (std::list<HeeksObj *>::iterator itLhs = lhs_children.begin(); itLhs != lhs_children.end(); itLhs++)
	{
	    HeeksObj *lhsPtr = *itLhs;
		bool l_bIntersectionsFound = false;	// If it's a circle and it doesn't
							// intersect anything else, we want to know
							// about it.

		if (lhsPtr->GetType() == PointType)
		{
			double pos[3];
			lhsPtr->GetStartPoint(pos);

			// Copy the results in ONLY if each point doesn't already exist.
			if (std::find( locations.begin(), locations.end(), CNCPoint( pos ) ) == locations.end())
			{
				locations.push_back( CNCPoint( pos ) );
			} // End if - then

			continue;	// No need to intersect a point with anything.
		} // End if - then

        for (std::list<HeeksObj *>::iterator itRhs = rhs_children.begin(); itRhs != rhs_children.end(); itRhs++)
        {
            HeeksObj *rhsPtr = *itRhs;

			if (lhsPtr == rhsPtr) continue;
			if (lhsPtr->GetType() == PointType) continue;	// No need to intersect a point type.

            std::list<double> results;

            if ((lhsPtr != NULL) && (rhsPtr != NULL) && (lhsPtr->Intersects( rhsPtr, &results )))
            {
				l_bIntersectionsFound = true;
                while (((results.size() % 3) == 0) && (results.size() > 0))
                {
                    CNCPoint intersection;

                    intersection.SetX( *(results.begin()) );
                    results.erase(results.begin());

                    intersection.SetY( *(results.begin()) );
                    results.erase(results.begin());

                    intersection.SetZ( *(results.begin()) );
                    results.erase(results.begin());

					// Copy the results in ONLY if each point doesn't already exist.
					if (std::find( locations.begin(), locations.end(), intersection ) == locations.end())
					{
						locations.push_back(intersection);
					} // End if - then
				} // End while
			} // End if - then
		} // End for

		if (! l_bIntersectionsFound)
		{
			// This element didn't intersect anything else.  If it's a circle
			// then add its centre point to the result set.

			if (lhsPtr->GetType() == CircleType)
			{
				double pos[3];
				if ((lhsPtr != NULL) && (heeksCAD->GetArcCentre( lhsPtr, pos )))
				{
					// Copy the results in ONLY if each point doesn't already exist.
					if (std::find( locations.begin(), locations.end(), CNCPoint( pos ) ) == locations.end())
					{
						locations.push_back( CNCPoint( pos ) );
					} // End if - then
				} // End if - then
			} // End if - then


			if (lhsPtr->GetType() == SketchType)
			{
				CBox bounding_box;
				lhsPtr->GetBox( bounding_box );
				double pos[3];
				bounding_box.Centre(pos);
				// Copy the results in ONLY if each point doesn't already exist.
				if (std::find( locations.begin(), locations.end(), CNCPoint( pos ) ) == locations.end())
				{
					locations.push_back( CNCPoint( pos ) );
				} // End if - then
			} // End if - then

			if (lhsPtr->GetType() == ProfileType)
			{
				std::vector<CNCPoint> starting_points;
				CMachineState machine;
#ifndef STABLE_OPS_ONLY
				CFixture perfectly_aligned_fixture(NULL,CFixture::G54, false, 0.0);
				machine.Fixture(perfectly_aligned_fixture);
#endif

				// to do, make this get the starting point again
				//((CProfile *)lhsPtr)->AppendTextToProgram( starting_points, &machine );

				// Copy the results in ONLY if each point doesn't already exist.
				for (std::vector<CNCPoint>::const_iterator l_itPoint = starting_points.begin(); l_itPoint != starting_points.end(); l_itPoint++)
				{
					if (std::find( locations.begin(), locations.end(), *l_itPoint ) == locations.end())
					{
						locations.push_back( *l_itPoint );
					} // End if - then
				} // End for
			} // End if - then

            if (lhsPtr->GetType() == DrillingType)
            {
                // Ask the Drilling object what reference points it uses.
                if ((((COp *) lhsPtr)->m_tool_number > 0) && (pToolNumbersReferenced != NULL))
                {
                    pToolNumbersReferenced->push_back( ((COp *) lhsPtr)->m_tool_number );
                } // End if - then

                std::vector<CNCPoint> holes = CDrilling::FindAllLocations((CDrilling *)lhsPtr, starting_location, false, pToolNumbersReferenced);
                for (std::vector<CNCPoint>::const_iterator l_itHole = holes.begin(); l_itHole != holes.end(); l_itHole++)
                {
                    if (std::find( locations.begin(), locations.end(), *l_itHole ) == locations.end())
                    {
                        locations.push_back( *l_itHole );
                    } // End if - then
                } // End for
            } // End if - then

#ifndef STABLE_OPS_ONLY
            if (lhsPtr->GetType() == CounterBoreType)
            {
                std::vector<CNCPoint> holes = CDrilling::FindAllLocations((CCounterBore *)lhsPtr, starting_location, false, NULL);
                for (std::vector<CNCPoint>::const_iterator l_itHole = holes.begin(); l_itHole != holes.end(); l_itHole++)
                {
                    if (std::find( locations.begin(), locations.end(), *l_itHole ) == locations.end())
                    {
                        locations.push_back( *l_itHole );
                    } // End if - then
                } // End for
            } // End if - then
#endif

		} // End if - then
	} // End for

	if (sort_locations)
	{
		// This drilling cycle has the 'sort' option turned on.
		//
		// If the sorting option is turned off then the points need to be returned in order of the m_symbols list.  One day,
		// we will allow the operator to re-order the m_symbols list by using a drag-n-drop operation on the sub-elements
		// in the menu.  When this is done, the operator's decision as to order should be respected.  Until then, we can
		// use the 'sort' option in the drilling cycle's parameters.

		for (std::vector<CNCPoint>::iterator l_itPoint = locations.begin(); l_itPoint != locations.end(); l_itPoint++)
		{
			if (l_itPoint == locations.begin())
			{
				// It's the first point.
				CNCPoint reference_location(0.0, 0.0, 0.0);
                reference_location = starting_location;

				sort_points_by_distance compare( reference_location );
				std::sort( locations.begin(), locations.end(), compare );
			} // End if - then
			else
			{
				// We've already begun.  Just sort based on the previous point's location.
				std::vector<CNCPoint>::iterator l_itNextPoint = l_itPoint;
				l_itNextPoint++;

				if (l_itNextPoint != locations.end())
				{
					sort_points_by_distance compare( *l_itPoint );
					std::sort( l_itNextPoint, locations.end(), compare );
				} // End if - then
			} // End if - else
		} // End for
	} // End if - then

	return(locations);
} // End FindAllLocations() method


/**
	This method adjusts any parameters that don't make sense.  It should report a list
	of changes in the list of strings.
 */
std::list<wxString> CDrilling::DesignRulesAdjustment(const bool apply_changes)
{
	std::list<wxString> changes;

	// Make some special checks if we're using a chamfering bit.
	if (m_tool_number > 0)
	{
		CTool *pChamfer = (CTool *) CTool::Find( m_tool_number );
		if (pChamfer != NULL)
		{
			std::vector<CNCPoint> these_locations = CDrilling::FindAllLocations(this);

			if (pChamfer->m_params.m_type == CToolParams::eChamfer)
			{
				// We need to make sure that the diameter of the hole (that will
				// have been drilled in a previous drilling operation) is between
				// the chamfering bit's flat_radius (smallest) and diamter/2 (largest).

				// First find ALL drilling cycles that created this hole.  Make sure
				// to get them all as we may have used a centre drill before the
				// main hole is drilled.

				for (HeeksObj *obj = theApp.m_program->Operations()->GetFirstChild();
					obj != NULL;
					obj = theApp.m_program->Operations()->GetNextChild())
				{
					if (obj->GetType() == DrillingType)
					{
						// Make sure we're looking at a hole drilled with something
						// more than a centre drill.
						CToolParams::eToolType type = CTool::CutterType( ((COp *)obj)->m_tool_number );
						if (	(type == CToolParams::eDrill) ||
							(type == CToolParams::eEndmill) ||
							(type == CToolParams::eSlotCutter) ||
							(type == CToolParams::eBallEndMill))
						{
							// See if any of the other drilling locations line up
							// with our drilling locations.  If so, we must be
							// chamfering a previously drilled hole.

							std::vector<CNCPoint> previous_locations = CDrilling::FindAllLocations((CDrilling *)obj);
							std::vector<CNCPoint> common_locations;
							std::set_intersection( previous_locations.begin(), previous_locations.end(),
										these_locations.begin(), these_locations.end(),
										std::inserter( common_locations, common_locations.begin() ));
							if (common_locations.size() > 0)
							{
								// We're here.  We must be chamfering a hole we've
								// drilled previously.  Check the diameters.

								CTool *pPreviousTool = CTool::Find( ((COp *)obj)->m_tool_number );
								if (pPreviousTool->CuttingRadius() < pChamfer->m_params.m_flat_radius)
								{
#ifdef UNICODE
									std::wostringstream l_ossChange;
#else
									std::ostringstream l_ossChange;
#endif
									l_ossChange << _("Chamfering bit for drilling op") << " (id=" << m_id << ") " << _("is too big for previously drilled hole") << " (drilling id=" << obj->m_id << ")\n";
									changes.push_back( l_ossChange.str().c_str() );
								} // End if - then

								if (pPreviousTool->CuttingRadius() > (pChamfer->m_params.m_diameter/2.0))
								{
#ifdef UNICODE
									std::wostringstream l_ossChange;
#else
									std::ostringstream l_ossChange;
#endif
									l_ossChange << _("Chamfering bit for drilling op") << " (id=" << m_id << ") " << _("is too small for previously drilled hole") << " (drilling id=" << obj->m_id << ")\n";
									changes.push_back( l_ossChange.str().c_str() );
								} // End if - then
							} // End if - then

						} // End if - then
					} // End if - then
				} // End for
			} // End if - then
		} // End if - then
	} // End if - then

	if (m_tool_number > 0)
	{
		// Make sure the hole depth isn't greater than the tool's cutting depth.
		CTool *pDrill = (CTool *) CTool::Find( m_tool_number );
		if ((pDrill != NULL) && (pDrill->m_params.m_cutting_edge_height < m_params.m_depth))
		{
			// The drill bit we've chosen can't cut as deep as we've setup to go.

			if (apply_changes)
			{
#ifdef UNICODE
				std::wostringstream l_ossChange;
#else
				std::ostringstream l_ossChange;
#endif

				l_ossChange << _("Adjusting depth of drill cycle") << " id='" << m_id << "' " << _("from") << " '"
					<< m_params.m_depth / theApp.m_program->m_units << "' " << _("to") << " "
					<< pDrill->m_params.m_cutting_edge_height / theApp.m_program->m_units << "\n";
				changes.push_back(l_ossChange.str().c_str());

				m_params.m_depth = pDrill->m_params.m_cutting_edge_height;
			} // End if - then
			else
			{
#ifdef UNICODE
				std::wostringstream l_ossChange;
#else
				std::ostringstream l_ossChange;
#endif

				l_ossChange << _("WARNING") << ": " << _("Drilling") << " (id=" << m_id << ").  " << _("Can't drill hole") << " " << m_params.m_depth / theApp.m_program->m_units << " when the drill bit's cutting length is only " << pDrill->m_params.m_cutting_edge_height << " long\n";
				changes.push_back(l_ossChange.str().c_str());
			} // End if - else
		} // End if - then
	} // End if - then

	// See if there is anything in the reference objects that may be in conflict with this object's current configuration.
	for (Symbols_t::const_iterator l_itSymbol = m_symbols.begin(); l_itSymbol != m_symbols.end(); l_itSymbol++)
	{
		switch (l_itSymbol->first)
		{
			case ProfileType:
				{
					CProfile *pProfile = (CProfile *) heeksCAD->GetIDObject( l_itSymbol->first, l_itSymbol->second );
					if (pProfile != NULL)
					{
                        double depthOp_depth = ((CDepthOp *) pProfile)->m_depth_op_params.m_start_depth  - ((CDepthOp *) pProfile)->m_depth_op_params.m_final_depth;
                        if (depthOp_depth != m_params.m_depth)
                        {
    #ifdef UNICODE
                    std::wostringstream l_ossChange;
    #else
                    std::ostringstream l_ossChange;
    #endif

                            l_ossChange << _("Adjusting depth of drill cycle") << " (id='" << m_id << "') " << _("from") << " '"
                                << m_params.m_depth / theApp.m_program->m_units << "' " << _("to") << " '"
                                << depthOp_depth  / theApp.m_program->m_units<< "'\n";
                            changes.push_back(l_ossChange.str().c_str());

                            if (apply_changes)
                            {
                                m_params.m_depth = depthOp_depth;
                            } // End if - then
                        } // End if - then
					}
				}
				break;

			default:
				break;
		} // End switch
	} // End for

	// see wether combination of retract_mode, spindle_mode and peck_depth is valid:
	// move to design rule check
	if ((m_params.m_retract_mode == 1) || (m_params.m_spindle_mode == 1))
	{
		// if we feed retract, or stop the spindle at the bottom, this is a boring cycle.
		// cant have peck_depth > 0 then
		if (m_params.m_peck_depth > 0)
		{
#ifdef UNICODE
			std::wostringstream l_ossChange;
#else
			std::ostringstream l_ossChange;
#endif

			l_ossChange << _("WARNING") << ": " << _("cant have boring cycle with pecking > 0") << " (id=" << m_id << ")\n";
			changes.push_back(l_ossChange.str().c_str());

		}
	}



	return(changes);

} // End DesignRulesAdjustment() method


/**
    This method returns TRUE if the type of symbol is suitable for reference as a source of location
 */
/* static */ bool CDrilling::ValidType( const int object_type )
{
    switch (object_type)
    {
        case PointType:
        case CircleType:
        case SketchType:
        case DrillingType:
        case ProfileType:
        case PocketType:
		case FixtureType:
		case ILineType:
            return(true);

        default:
            return(false);
    }
}


void CDrilling::GetTools(std::list<Tool*>* t_list, const wxPoint* p)
{
    CSpeedOp::GetTools( t_list, p );
}


bool CDrillingParams::operator==( const CDrillingParams & rhs) const
{
	if (m_standoff != rhs.m_standoff) return(false);
	if (m_dwell != rhs.m_dwell) return(false);
	if (m_depth != rhs.m_depth) return(false);
	if (m_peck_depth != rhs.m_peck_depth) return(false);
	if (m_sort_drilling_locations != rhs.m_sort_drilling_locations) return(false);
	if (m_retract_mode != rhs.m_retract_mode) return(false);
	if (m_spindle_mode != rhs.m_spindle_mode) return(false);
	if (m_clearance_height != rhs.m_clearance_height) return(false);

	return(true);
}


bool CDrilling::operator==( const CDrilling & rhs ) const
{
	if (m_params != rhs.m_params) return(false);

	return(CSpeedOp::operator==(rhs));
}

double CDrillingParams::ClearanceHeight() const
{
	switch (theApp.m_program->m_clearance_source)
	{
	case CProgram::eClearanceDefinedByMachine:
		return(theApp.m_program->m_machine.m_clearance_height);

	case CProgram::eClearanceDefinedByFixture:
		// We need to figure out which is the 'active' fixture and return
		// the clearance height from that fixture.

		if (theApp.m_program->m_active_machine_state != NULL)
		{
			return(theApp.m_program->m_active_machine_state->Fixture().m_params.m_clearance_height);
		}
		else
		{
			// This should not occur.  In any case, use the clearance value from the individual operation.
			return(m_clearance_height);
		}

	case CProgram::eClearanceDefinedByOperation:
	default:
		return(m_clearance_height);
	} // End switch
}
