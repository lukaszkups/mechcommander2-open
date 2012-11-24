#define LOGISTICSDATA_CPP
/*************************************************************************************************\
LogisticsData.cpp			: Implementation of the LogisticsData component.
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//
\*************************************************************************************************/

#include "LogisticsData.h"
#include "file.h"
#include "LogisticsErrors.h"
#include "cmponent.h"
#include "paths.h"
#include "warrior.h"
#include "..\resource.h"
#include "malloc.h"
#include "Team.h"
#include "Mech.h"
#include "LogisticsMissionInfo.h"
#include "packet.h"
#include "gamesound.h"
#include "prefs.h"
#include "comndr.h"
#include "missionresults.h"
#include "zlib.h"
//#include "afx.h"

#ifndef VIEWER
	#include "multPlyr.h"
	#include "Chatwindow.h"
#else
	bool MissionResults::FirstTimeResults = false;
#endif

extern CPrefs prefs;

//----------------------------------------------------------------------
// WARNING!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//PLEASE CHANGE THIS IF THE SAVEGAME FILE FORMAT CHANGES!!!!!!!!!!!!!
long SaveGameVersionNumber = 10013; //magic 02062012
//----------------------------------------------------------------------

LogisticsData* LogisticsData::instance = NULL;

//*************************************************************************************************
LogisticsData::LogisticsData()
{
	gosASSERT( !instance );
	instance = this;
	resourcePoints = 0;

	currentlyModifiedMech = 0;
	missionInfo = 0;
	rpJustAdded = false;
	loadFromSave = false;
	loadSoloMission = false;

	bNewMechs = bNewWeapons = bNewPilots = 0;
}

LogisticsData::~LogisticsData()
{
	for( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
	{
		delete( *iter );
	}

	for ( VEHICLE_LIST::EIterator vIter = vehicles.Begin(); !vIter.IsDone(); vIter++ )
	{
		delete (*vIter );
	}

	for ( MECH_LIST::EIterator mIter = inventory.Begin(); !mIter.IsDone(); mIter++ )
	{
		delete (*mIter);
	}

/*	//Magic begin create error on exit game
	for ( ICOMPONENT_LIST::EIterator cIter = icomponents.Begin(); !cIter.IsDone(); cIter++ )
	{
		delete (*cIter);
	}
	for( VARIANT_LIST::EIterator iter = purvariants.Begin(); !iter.IsDone(); iter++ )
	{
		delete( *iter );
	}*/

	//Magic end


	variants.Clear();
	vehicles.Clear();
	purvariants.Clear();
//M24
	inventory.Clear();
	icomponents.Clear();
//M24
	salvageComponents.Clear(); //magic 04092012

	delete missionInfo;
	missionInfo = NULL;

#ifndef VIEWER
	ChatWindow::destroy();
#endif
}

//*************************************************************************************************
void LogisticsData::init()
{
	if ( components.Count() ) // already been initialized
	{
		return;
	}
	
	initComponents();
	initPilots();
	initVariants();

	missionInfo = new LogisticsMissionInfo;

	
	FitIniFile file;
	if ( NO_ERR != file.open( "data\\campaign\\campaign.fit" ) )
	{
		Assert( 0, 0, "coudln't find the campaign file\n" );
	}
	missionInfo->init( file );
	 //magic 25082012 they load campagn on init and set mission one as default for init?

	// temporary, just so we can test
	int count = 32;
	const char* missionNames[32];
	missionInfo->getAvailableMissions( missionNames, count );

	setCurrentMission( missionNames[0] );
}

//*************************************************************************************************
void LogisticsData::initComponents()
{
	char componentPath[256];
	strcpy( componentPath, objectPath );
	strcat( componentPath, "compbas.csv" );

	
	File componentFile;
#ifdef _DEBUG
	int result = 
#endif
		componentFile.open( componentPath );
	gosASSERT( result == NO_ERR );

	BYTE* data = new BYTE[componentFile.getLength()];

	componentFile.read( data, componentFile.getLength() );


	File dataFile;
	dataFile.open( (char*)data, componentFile.getLength() );

	componentFile.close();
	
	BYTE line[1024];
	char* pLine = (char*)line;

	// skip the first line
	dataFile.readLine(line, 1024);

	int		Ammo[512];
	memset( Ammo, 0, sizeof ( int ) * 512 );


	LogisticsComponent tmpComp;
	int counter = 0;

	while(true)
	{
		int result = dataFile.readLine( line, 1023 );

		if ( result < 2 || result == 0xBADF0008 || result > 1024  )
			break;

		components.Append( tmpComp );
		
		LogisticsComponent& tmp = components.GetTail();

		/*
		if ( -1 == tmp.init( pLine ) ) // failure
		{
			Ammo[counter] = (long)tmp.getRecycleTime();
			components.DeleteTail();
		}
		*/ //magic 10022012 disable
		//magic 10022012 begin
		
		if ( -1 == tmp.init( pLine ) ) // failure
		{
			components.DeleteTail();
		}
		else
			if (tmp.getType() == COMPONENT_FORM_AMMO) //magic 10022012 we have ammo
			{
				Ammo[counter] = (long)tmp.getRecycleTime();
			}
		//magic 10022012 end
		counter++;
	}

	// fix up ammo
	for ( COMPONENT_LIST::EIterator iter = components.Begin(); !iter.IsDone(); iter++ )
	{
		if ((*iter).getAmmo() )
		{
			(*iter).setAmmo( Ammo[(*iter).getAmmo()] );
		}
	}

	delete [] data;
	data = NULL;
}

//*************************************************************************************************
void LogisticsData::initPilots()
{

	pilots.Clear();

	char pilotPath[256];
	strcpy( pilotPath, objectPath );
	strcat( pilotPath, "pilots.csv" );

	
	File pilotFile;
	pilotFile.open( pilotPath );

	BYTE pilotFileName[256];

	int id = 1;

	while( true )
	{
		int bytesRead = pilotFile.readLine( pilotFileName, 256 );
		
		if ( bytesRead < 2 )
			break;

		LogisticsPilot tmpPilot;
		pilots.Append( tmpPilot );
		LogisticsPilot& pilot = pilots.GetTail();
		pilot.id = id;

		if ( -1 == pilot.init( (char*)pilotFileName ) )
			pilots.DeleteTail();

		id++;

	}
}

void LogisticsData::initVariants()
{
	char variantPath[256];
	strcpy( variantPath, artPath );
	strcat( variantPath, "buildings.csv" );


	CSVFile variantFile;
	variantFile.open( variantPath );

	FullPathFileName pakPath;
	pakPath.init( objectPath, "Object2", ".pak" );

	PacketFile pakFile;
	
	if ( NO_ERR !=pakFile.open( pakPath ) )
	{
		char errorStr[256];
		sprintf( errorStr, "couldn't open file %s", (char*)pakPath );
		Assert( 0, 0, errorStr );
	}


	char variantFileName[256];
	char variantFullPath[1024];

	int chassisID = 0;

	char tmpStr[256];

	int i = 1;
	while( true )
	{
		long fitID;

		int retVal = variantFile.readString( i, 4, tmpStr, 256 );
		
		if ( retVal != 0 )
			break;

		if ( stricmp( tmpStr, "VEHICLE" ) == 0 )
		{
			float scale;
			variantFile.readFloat( i, 11, scale );
			if ( scale )
			{
				variantFile.readLong( i, 5, fitID );
				addVehicle( fitID, pakFile, scale);
			}
			i++;
			continue;
		}
		if ( stricmp( tmpStr, "MECH" ) != 0 )
		{
		
			float scale;
			if ( NO_ERR == variantFile.readFloat( i, 11, scale ) && scale )
			{
				variantFile.readLong( i, 5, fitID );
				addBuilding( fitID, pakFile, scale );
			}
			
			i++;
			continue;

		}
		
		float scale;
		if ( NO_ERR != variantFile.readFloat( i, 11, scale ))
			scale = 1.0;

		variantFile.readString( i, 1, variantFileName, 256 );

		variantFile.readLong( i, 5, fitID );

		strcpy( variantFullPath, objectPath );
		strcat( variantFullPath, variantFileName );
		strcat(  variantFullPath, ".csv" );
		_strlwr( variantFullPath );

		CSVFile mechFile;
		if ( NO_ERR != mechFile.open( variantFullPath ) )
		{
			char error[256];
			sprintf( error, "couldn't open file %s", variantFullPath );
			Assert( 0, 0, error );
			return;
		}

		LogisticsChassis* chassis = new LogisticsChassis();
		chassis->init( &mechFile, chassisID++ );
		chassis->setFitID(fitID);
		chassis->setScale( scale );

		int row = 23;
		char buffer[256];
		int varCount = 0;
		while( NO_ERR == mechFile.readString( row, 2, buffer, 256 ) )
		{
			LogisticsVariant* pVariant = new LogisticsVariant;
			
			if ( 0 == pVariant->init( &mechFile, chassis, varCount++ ) )
				variants.Append( pVariant );
			else
				delete pVariant;

			row += 97;
		}

		i++;

	}
}

void LogisticsData::addVehicle( long fitID, PacketFile& objectFile, float scale )
{
	if ( NO_ERR != objectFile.seekPacket(fitID) )
		return;

	int fileSize = objectFile.getPacketSize();

	if ( fileSize )
	{
		LogisticsVehicle* pVehicle = new LogisticsVehicle;

		FitIniFile file;
		 file.open(&objectFile, fileSize);

		pVehicle->init( file );
		pVehicle->setScale( scale );
		vehicles.Append( pVehicle );
	}
}


//*************************************************************************************************
void LogisticsData::RegisterFunctions()
{
	

}

//*************************************************************************************************
void LogisticsData::UnRegisterFunctions()
{

}


int LogisticsData::getAvailableComponents( LogisticsComponent** pComps, int& maxCount )
{
	int retVal = 0;
	
	int i = 0;
	for ( COMPONENT_LIST::EIterator iter = components.Begin(); 
		!iter.IsDone(); iter++ )
		{
			if ( (*iter).isAvailable()  )
			{
				if ( i < maxCount )
					pComps[i]	= &(*iter);	
				else 
					retVal = NEED_BIGGER_ARRAY;
				++i;

				
			}
		}

	maxCount = i;

	return retVal; 

}
int	LogisticsData::getAllComponents( LogisticsComponent** pComps, int& maxCount )
{
	int retVal = 0;
	
	int i = 0;
	for ( COMPONENT_LIST::EIterator iter = components.Begin(); 
		!iter.IsDone(); iter++ )
		{
			if ( i < maxCount )
					pComps[i]	= &(*iter);	
				else 
					retVal = NEED_BIGGER_ARRAY;
				++i;
		}

	maxCount = components.Count();

	return retVal; 
}




int LogisticsData::getPurchasableMechs( LogisticsVariant** array, int& count )
{
	long retVal = 0;
	long arraySize = count;

	count = 0;
	for( VARIANT_LIST::EIterator iter = instance->variants.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->isAvailable() )
		{
			if ( count >= arraySize )
			{
				retVal =  NEED_BIGGER_ARRAY; // keep counting
			}
			else
			{
				array[count] = (*iter);
			}

			(count)++;	
		}
	}

	return retVal;

}




int LogisticsData::purchaseMech( LogisticsVariant* pVariant )
{
	if ( !pVariant )
		return -1;

	int RP = pVariant->getCost();

	if ( missionInfo->getCBills() - RP >= 0 )
	{
		int count = instance->createInstanceID( pVariant );
		LogisticsMech* pMech = new LogisticsMech( pVariant, count );
		instance->inventory.Append( pMech );
		missionInfo->decrementCBills( pVariant->getCost() );
		return 0;
	}

	return NOT_ENOUGH_RESOURCE_POINTS;
}

int LogisticsData::canPurchaseMech( LogisticsVariant* pVar )
{
	if ( !pVar )
		return INVALID_ID;

	int RP = pVar->getCost();

	if ( missionInfo->getCBills() - RP >= 0 )
	{
		return 0;
	}

	return NOT_ENOUGH_RESOURCE_POINTS;

}



int LogisticsData::sellMech( LogisticsMech* pVar )
{
	if ( !pVar )
		return -1;

	for ( MECH_LIST::EIterator iter = instance->inventory.End(); !iter.IsDone(); iter-- )
	{
		if ( (*iter)->getForceGroup() )
			continue;
		if ( (*iter)->getVariant() == pVar->getVariant() )
		{
			int cost = ((*iter))->getCost();
			(*iter)->setPilot( NULL );
			delete *iter;
			instance->inventory.Delete( iter );
			missionInfo->incrementCBills( cost );
			return 0;
		}
	}

	return -1;
}

int LogisticsData::removeVariant( const char* varName )
{
	if ( !varName )
		return -1;

	LogisticsVariant* pVar = 0;

	if ( currentlyModifiedMech->getName() == varName || oldVariant->getName() == varName )
		return -1;

	for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
	{
		if ( (*vIter)->getName().Compare( varName, 0 ) == 0 )
		{
			pVar = (*vIter );
			break;
		}
	}

	if ( !pVar )
	{
		return INVALID_VARIANT;
	}

	for ( MECH_LIST::EIterator iter = instance->inventory.End(); !iter.IsDone(); iter-- )
	{
		if ( (*iter)->getVariant() == pVar )
		{
			return VARIANT_IN_USE;
		}
	}

	delete pVar;
	variants.Delete( vIter );

	return 0;
}



int LogisticsData::createInstanceID( LogisticsVariant* pVar )
{
	int count = -1;
	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( pVar->getVariantID() == (*iter)->getVariantID() ) 
		{
			int tmp = (*iter)->getInstanceID();
			if ( tmp > count )
				count = tmp;
		}
	}
	return count + 1;
}

LogisticsVariant* LogisticsData::getVariant( int ID )
{
	for ( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getID() == (ID & 0x00ffffff) )
		{
			return *iter;
		}
	}

	if ( instance->currentlyModifiedMech && ID == instance->currentlyModifiedMech->getID() )
		return instance->currentlyModifiedMech->getVariant();

	return NULL;
}

LogisticsMech*	LogisticsData::getMech( int ID )
{
	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getID() == ID )
			return (*iter );
	}

	return NULL;
}




int LogisticsData::addMechToForceGroup( LogisticsMech* pMech, int slot )
{
	if ( !pMech )
		return -1;

	if ( slot > 12 )
		return -1;


	if ( pMech && !pMech->getForceGroup() )
	{
		pMech->setForceGroup( slot );
		return 0;
	}
	else // find another of the same variant
	{

		LogisticsMech* pNewMech = getMechWithoutForceGroup( pMech );
		if ( pNewMech )
		{
			pNewMech->setForceGroup( slot );
			return 0;
		}
	}
	return -1;
}

int		LogisticsData::removeMechFromForceGroup( int slot )
{
	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getForceGroup() == slot )
		{
			(*iter)->setForceGroup( 0 );
			(*iter)->setPilot( 0 );
			return 0;
		}
	}

	return -1;

}

LogisticsMech*		LogisticsData::getMechWithoutForceGroup( LogisticsMech* pMech )
{
	if ( !pMech->getForceGroup() )
		return pMech;

	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
		{
			if ( (*iter)->getVariant() == pMech->getVariant() && !(*iter)->getForceGroup() )
			{
				return (*iter);
				
			}
		}
	
	return NULL;
}
int LogisticsData::removeMechFromForceGroup( LogisticsMech* pMech, bool bRemovePilot )
{
	if ( !pMech )
		return -1;

	if ( pMech && pMech->getForceGroup() )
	{
		pMech->setForceGroup( 0 );
		// no mechs in inventory have pilots
		if ( bRemovePilot )
			pMech->setPilot( 0 );
		return 0;
	}

	// find similar one
	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getVariant() == pMech->getVariant() && (*iter)->getForceGroup() )
		{
			(*iter)->setForceGroup( 0 );

			if ( bRemovePilot )
				pMech->setPilot( 0 );

			return 0;
		}
	}
	return -1;
}

LogisticsPilot* LogisticsData::getFirstAvailablePilot()
{
	for ( PILOT_LIST::EIterator iter = pilots.Begin(); !iter.IsDone(); iter++  )
	{
		 bool bIsUsed = false;
		 for ( MECH_LIST::EIterator mIter = inventory.Begin(); !mIter.IsDone(); mIter++ )
		 {
			LogisticsPilot* pPilot = (*mIter)->getPilot();
			if ( pPilot && pPilot->getID() == (*iter).getID() )
			{
				bIsUsed = true;
				break;
			}
		 }
		 if ( !bIsUsed )
		 {
			 return &(*iter);
		 }
	}

	return NULL;

}



// GetAvailableMissions( char** missionNames, long& count )
int LogisticsData::getAvailableMissions( const char** missionNames, long& count )
{
	int numberOfEm = 0;

	// first figure out how many there are
	missionInfo->getAvailableMissions( 0, numberOfEm );

	// next make sure the array is big enough
	if ( count < numberOfEm )
		return NEED_BIGGER_ARRAY;

	missionInfo->getAvailableMissions( missionNames, numberOfEm );
	count= numberOfEm;
	
	return 0;



}

int LogisticsData::getCurrentMissions( const char** missionNames, long& count )
{
	int numberOfEm = 0;

	// first figure out how many there are
	missionInfo->getCurrentMissions( 0, numberOfEm );

	// next make sure the array is big enough
	if ( count < numberOfEm )
		return NEED_BIGGER_ARRAY;

	numberOfEm = count;

	missionInfo->getCurrentMissions( missionNames, numberOfEm );
	count= numberOfEm;
	
	return 0;



}

bool LogisticsData::getMissionAvailable( const char* missionName )
{
	return missionInfo->getMissionAvailable( missionName );
}


// SetCurrentMission( char* missionName )
int LogisticsData::setCurrentMission( const char* missionName )
{
	long result = missionInfo->setNextMission( missionName );

	if ( result == NO_ERR )
	{
		// if we made it this far
		updateAvailability();

		resourcePoints = missionInfo->getCurrentRP();

		removeDeadWeight();
	}


	

	return result;
}

void LogisticsData::removeDeadWeight()
{
	int maxDropWeight = getMaxDropWeight();
	int curDropWeight = getCurrentDropWeight();

	int i = 12;
	while ( curDropWeight > maxDropWeight )
	{
		LogisticsData::removeMechFromForceGroup( i );
		i--;

		curDropWeight = getCurrentDropWeight();

		if ( i == 0 )
			break;
	}
}

int		LogisticsData::setCurrentMission( const EString& missionName )
{
	return setCurrentMission( (const char*)missionName );
}


void	LogisticsData::getForceGroup( EList<LogisticsMech*, LogisticsMech*>& newList )
{
	int count = 0;

	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( count > 11 )
			break;

		if ( (*iter)->getForceGroup() )
		{
			newList.Append( (*iter) );
			count++;
		}
	}
}

void	LogisticsData::getInventory( EList<LogisticsMech*, LogisticsMech*>& newList )
{
	for ( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{

		newList.Append( (*iter) );
	}
}

void	LogisticsData::getAllVariants( EList<LogisticsVariant*, LogisticsVariant*>& newList )
{
	for ( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
	{

		newList.Append( (*iter) );
	}
}


void	LogisticsData::addMechToInventory( LogisticsVariant* pVar, LogisticsPilot* pPilot, int ForceGroup,
										  bool bSubtractPts)
{
	if ( pVar )
	{
		int count = instance->createInstanceID( pVar );
		LogisticsMech* pMech = new LogisticsMech( pVar, count );
		inventory.Append( pMech );
		pMech->setForceGroup( ForceGroup );
		if ( pPilot && !pPilot->isDead() )
			pMech->setPilot( pPilot );
		if ( ForceGroup > -1 && ForceGroup < 13 )
			pMech->setForceGroup( ForceGroup );
		if ( bSubtractPts )
			missionInfo->decrementCBills( pMech->getCost() );
	}
}
void	LogisticsData::addMechToInventory( LogisticsVariant* pVar, int addToForceGroup, 
										  LogisticsPilot* pPilot,
										  unsigned long baseColor,
										  unsigned long highlight1,
										  unsigned long highlight2 )
{
	if ( pVar )
	{
		int count = instance->createInstanceID( pVar );
		LogisticsMech* pMech = new LogisticsMech( pVar, count );
		inventory.Append( pMech );
		//magic 13052012 begin
		if (!(pVar->getInternalDamage() > 0))
		{
		//magic 13052012 end
		if ( addToForceGroup > -1 && addToForceGroup < 13 )
			pMech->setForceGroup( addToForceGroup );
		if ( pPilot && !pPilot->isDead() )
			pMech->setPilot( pPilot );

		//magic 13052012 begin
		}
		//magic 13052012 end
		pMech->setColors( baseColor, highlight1, highlight2 );
		return;
	}
	else
	{
		gosASSERT(!"couldn't add the mech to the inventory" );
	}
}

LogisticsVariant* LogisticsData::getVariant( const char* pCSVFileName, int VariantNum )
{
	EString lowerCase = pCSVFileName;
	lowerCase.MakeLower();
	for( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
	{
		if ( -1 !=( (*iter)->getFileName().Find( lowerCase, -1 ) )
			&& (((*iter)->getVariantID()>>16)&0xff) == VariantNum )
		{
			return *iter;
		}
	}

	return NULL;
}

void LogisticsData::removeMechsInForceGroup()
{
	if ( !inventory.Count() )
		return;
	for ( MECH_LIST::EIterator iter = inventory.End(); !iter.IsDone();  )
	{
		if ( (*iter)->getForceGroup() )
		{
			MECH_LIST::EIterator tmpIter = iter;
			iter--;
			delete *tmpIter;
			inventory.Delete( tmpIter );
		}
		else
			iter--;
	}
}

const char*	LogisticsData::getBestPilot( long mechWeight )
{
	if ( !pilots.Count() )
		initPilots();

	LogisticsPilot** pPilots = (LogisticsPilot**)_alloca( pilots.Count() * sizeof( LogisticsPilot*) );
	memset( pPilots, 0, pilots.Count() * sizeof( LogisticsPilot*) );

	int counter = 0;
#ifndef VIEWER
	for ( PILOT_LIST::EIterator iter = pilots.Begin();
		!iter.IsDone(); iter++ )
		{
			const char *nameCheck = (*iter).getName();
			if ( (*iter).isAvailable() && (MPlayer || !MechWarrior::warriorInUse((char *)nameCheck)) )
				pPilots[counter++] = &(*iter);
		}
#endif

	long count = counter;

	for ( int i = 1; i < count; ++i )
	{
		LogisticsPilot* cur = pPilots[i];
		for ( int j = 0; j < i; ++j )
		{
			if ( comparePilots( cur, pPilots[j], mechWeight ) > 0 && j != i )
			{
				pPilots[i] = pPilots[j];
				pPilots[j] = cur;
				break;
			}
		}
	}

	for ( i = 0; i < count; i++ )
	{
		if ( pPilots[i]->isAvailable() && !pPilots[i]->isUsed() )
		{
			pPilots[i]->setUsed( 1 );
			return pPilots[i]->getFileName();
		}
	}

	gosASSERT( !"We're out of pilots, shouldn't be here" );
	
	pPilots[0]->setUsed( true );
	return pPilots[0]->getFileName();


}

bool		LogisticsData::gotPilotsLeft()
{
	if ( !pilots.Count() )
		initPilots();

	LogisticsPilot** pPilots = (LogisticsPilot**)_alloca( pilots.Count() * sizeof( LogisticsPilot*) );
	memset( pPilots, 0, pilots.Count() * sizeof( LogisticsPilot*) );

	int counter = 0;

	#ifndef VIEWER

	for ( PILOT_LIST::EIterator iter = pilots.Begin();
		!iter.IsDone(); iter++ )
		{
			const char *nameCheck = (*iter).getName();
			if ( (*iter).isAvailable() && ( MPlayer || !MechWarrior::warriorInUse((char *)nameCheck)) )
				pPilots[counter++] = &(*iter);
		}

	#endif
	long count = counter;

	for ( int i = 0; i < count; i++ )
	{
		if ( pPilots[i]->isAvailable() && !pPilots[i]->isUsed() )
		{
			return 1;
		}
	}

	return 0;

}

int LogisticsData::comparePilots( LogisticsPilot* p1, LogisticsPilot* p2, long weight )
{
	if ( p1->isUsed() )
		return -1;
	else if ( p2->isUsed() )
		return 1;
	
		for ( MECH_LIST::EIterator mIter = instance->inventory.Begin(); !mIter.IsDone(); mIter++ )
		 {
			LogisticsPilot* pPilot = (*mIter)->getPilot();
			if ( pPilot )
			{
				if ( pPilot->getID() == p1->getID()  )
					return -1;
				else if ( pPilot->getID() == p2->getID() )
					return 1;
			}
		 }



	if ( p1->getRank() > p2->getRank() )
		return 1;
	
	//else if ( p2->getRank() < p1->getRank() )
	else if ( p2->getRank() > p1->getRank() ) //magic 01122011
		return -1;

	// need to check specialty skill text for weight, not really done yet

	else if ( p2->getGunnery() > p1->getGunnery() )
		return -1;

	else if ( p1->getGunnery() > p2->getGunnery() )
		return 1;

	else if ( p1->getPiloting() > p2->getPiloting() )
		return 1;

	else if ( p2->getPiloting() > p1->getPiloting() )
		return -1;
//Magic 55 begin
	else if ( p1->getSensorSkill() > p2->getSensorSkill() )
		return 1;

	else if ( p2->getSensorSkill() > p1->getSensorSkill() )
		return -1;

	else if ( p1->getJumping() > p2->getJumping() )
		return 1;

	else if ( p2->getJumping() > p1->getJumping() )
		return -1;
//Magic 55 end
	return 0;


}

long	LogisticsData::save( FitIniFile& file )
{
		
	int variantCount = 0;
	// save the player created variants
	for ( VARIANT_LIST::EIterator vIter = variants.Begin();
		!vIter.IsDone();  vIter++ )
		{
			if ( !(*vIter)->isDesignerMech() )
			{
				(*vIter)->save( file, variantCount );
				variantCount++;
			}
		}

	//magic 24122010 begin
	long purMechsCount = 0;
	//bool mechPurFound = false;
	FullPathFileName mPS_FullFileName;
	 mPS_FullFileName.init(missionPath, "mechPurchase", ".fit");
	FitIniFile tempMechPurchaseFile;

	long result1 = tempMechPurchaseFile.open( mPS_FullFileName);
	if (result1 == NO_ERR)
	{
		tempMechPurchaseFile.seekBlock( "General" );
		tempMechPurchaseFile.readIdLong( "PurMechsCount", purMechsCount );
		//mechPurFound = true;
	}
	//magic 24122010 end

	file.writeBlock( "Version" );
	file.writeIdLong( "VersionNumber", SaveGameVersionNumber);

	file.writeBlock( "General" );	
	
	file.writeIdLong( "VariantCount", variantCount );
	file.writeIdLong( "PilotCount", pilots.Count() );
	file.writeIdLong( "InventoryCount", inventory.Count() );
	file.writeIdLong( "IComponentCount", icomponents.Count() ); //Magic
	file.writeIdLong( "PurComponentCount", purcomponents.Count() ); //Magic
	//file.writeIdLong( "PurMechsCount", purvariants.Count() ); //Magic
	file.writeIdLong( "PurMechsCount", purMechsCount ); //Magic 24122010
	file.writeIdBoolean( "FirstTimeResults", MissionResults::FirstTimeResults);
	//magic 02062012 begin
	//magic 10102012 begin
	if (!campaignOver())
	{
	//magic 10102012 end
		file.writeIdLong( "maxDropWeight", getMaxDropWeight() );
		file.writeIdLong( "missionRP", getCurrentRP() );
	} //magic 10102012
	//magic 02062012 end

	// save the campaign info
	missionInfo->save( file );

	int pilotCount = 0;
	// save the pilots
	for ( PILOT_LIST::EIterator pIter = pilots.Begin();
		!pIter.IsDone(); pIter++ )
		{
			(*pIter).save( file, pilotCount++ );
		}

	int mechCount = 0;
	// save the inventory
	for ( MECH_LIST::EIterator mIter = inventory.Begin();
		!mIter.IsDone(); mIter++ )
		{
			(*mIter)->save( file, mechCount++ );
		}

	int IcomponentCount = 0;
	file.writeBlock( "IComponents" );
	// save components inventory
	for ( ICOMPONENT_LIST::EIterator cIter = icomponents.Begin();
		!cIter.IsDone(); cIter++ )
		{
			char tmp[32];
			sprintf( tmp, "IComponent%ld", IcomponentCount );
			file.writeIdLong( tmp, (*cIter)->getID() );
			IcomponentCount++;
			//(*cIter)->save( file, IcomponentCount++ );
		}
//M18 begin
	//magic 24122010 begin
	if (purMechsCount > 0)
	{
		for ( long i = 0; i < purMechsCount; i++ )
		{
			char tmp[64];
			sprintf( tmp, "MechPurchase%ld", i ) ;
			if ( NO_ERR != tempMechPurchaseFile.seekBlock( tmp ) )
			{
				gosASSERT( 0 );
			}
			file.writeBlock( tmp );

			char tmp2[256];
			long qForPurchase;
			long result = tempMechPurchaseFile.readIdString( "Chassis", tmp2, 255 );
			file.writeIdString( "Chassis", tmp2 );

			result = tempMechPurchaseFile.readIdString( "Variant", tmp2, 255 );
			if (result == NO_ERR)
			{
				file.writeIdString( "Variant", tmp2 );
				long result2 = tempMechPurchaseFile.readIdLong( "Quantity", qForPurchase );
				if (result2 != NO_ERR)
					qForPurchase = 1;

				if ( (qForPurchase < 0) && (qForPurchase > 5))
					qForPurchase = 1;
			file.writeIdLong( "Quantity", qForPurchase );
			}
		}
	}
	tempMechPurchaseFile.close();
	//magic 24122010 end
	/*int purMechCount = 0;
	// save mech purchase
	for ( VARIANT_LIST::EIterator mIter = purvariants.Begin();
		!mIter.IsDone(); mIter++ )
		{
			LogisticsMech* mMech = new LogisticsMech( (*mIter), 1 );
			mMech->savePurchase( file, purMechCount++ );
		}*/

	int purComponentCount = 0;
	file.writeBlock( "purComponents" );
	// save components inventory
	for ( ICOMPONENT_LIST::EIterator cIter = purcomponents.Begin();
		!cIter.IsDone(); cIter++ )
		{
			char tmp[32];
			sprintf( tmp, "PComponent%ld", purComponentCount );
			file.writeIdLong( tmp, (*cIter)->getID() );
			purComponentCount++;
			//(*cIter)->save( file, IcomponentCount++ );
		}
//M18 end
	return 0;
}

void LogisticsData::clearVariants()
{
	for ( VARIANT_LIST::EIterator iter = variants.End(); !iter.IsDone();  )
	{
		if ( !(*iter)->isDesignerMech() )
		{
			VARIANT_LIST::EIterator tmpIter = iter;
			iter --;
			delete *tmpIter;
			variants.Delete( tmpIter );
	
		}
		else
			iter--;
	}
		
}

long	LogisticsData::load( FitIniFile& file )
{
	clearInventory();
	icomponents.Clear(); //Magic
	purcomponents.Clear(); //Magic
	purvariants.Clear(); //Magic
	//salvageComponents.Clear(); //magic 04092012 NO should not be here clear on mision begin and campaign begin

	resourcePoints = 0;
	pilots.Clear();
//	initPilots();
	clearVariants();
	initPilots(); //magic 02062012 moved from above

	if ( !missionInfo )
		missionInfo = new LogisticsMissionInfo;

	missionInfo->load( file );

	long result = file.seekBlock( "Version" );
	if (result != NO_ERR)
	{
		PAUSE(("SaveGame has no version number.  Not Loading"));
		return -1;
	}

	long testVersionNum = 0;
	result = file.readIdLong( "VersionNumber", testVersionNum);
	if (result != NO_ERR)
	{
		PAUSE(("SaveGame has no version number.  Not Loading"));
		return -1;
	}

	if (testVersionNum != SaveGameVersionNumber)
	{
		PAUSE(("SaveGame is not Version %d.  It was Version %d which is not valid!",SaveGameVersionNumber,testVersionNum));
		return -1;
	}

	file.seekBlock( "General" );	
	
	long variantCount, pilotCount, inventoryCount, iComponentCount;
	long purComponentCount, purMechsCount;
	variantCount = pilotCount = inventoryCount = iComponentCount = 0;
	purComponentCount = purMechsCount = 0;

	file.readIdLong( "VariantCount", variantCount );
	file.readIdLong( "PilotCount", pilotCount );
	file.readIdLong( "InventoryCount", inventoryCount );
	file.readIdLong( "IComponentCount", iComponentCount );//Magic
	file.readIdLong( "PurComponentCount", purComponentCount );//Magic
	file.readIdLong( "PurMechsCount", purMechsCount );//Magic
	file.readIdBoolean( "FirstTimeResults", MissionResults::FirstTimeResults);
	//magic 02062012 begin
	long maxDW;
	long curRP;
	file.readIdLong( "maxDropWeight", maxDW );
	file.readIdLong( "missionRP", curRP );
	setMaxDropWeight(maxDW);
	setRP(curRP);
	//magic 02062012 end

	char tmp[64];

	// load variants
	for ( int i = 0; i < variantCount; i++ )
	{
		sprintf( tmp, "Variant%ld", i );
		file.seekBlock( tmp );
		result = loadVariant( file );
		if ( result != NO_ERR )
		{
			gosASSERT( 0 );
			return -1;
		}
	}

	// load pilots
	for ( i = 0; i < pilotCount; i++ )
	{
		sprintf( tmp, "Pilot%ld", i );
		if ( NO_ERR != file.seekBlock( tmp ) )
		{
			gosASSERT( 0 );
		}

		file.readIdString( "FileName", tmp, 255 );

		// pilot should already exist
		for ( PILOT_LIST::EIterator pIter = pilots.Begin(); !pIter.IsDone(); pIter++ )
		{
			if ( (*pIter).getFileName().Compare( tmp, 0 ) == 0 )
			{
				(*pIter).load( file );
			}

			(*pIter).setAvailable( true );
		}
	}

	// load inventory
	int count = 0;
	for ( i = 0; i < inventoryCount; i++ )
	{
		sprintf( tmp, "Inventory%ld", i ) ;
		if ( NO_ERR != file.seekBlock( tmp ) )
		{
			gosASSERT( 0 );
		}

		loadMech( file, count );
	}

//Magic begin load components inventory


	long result1 = file.seekBlock( "IComponents" );
	if ( result1 != NO_ERR )
	{
		Assert( 0, 0, "No components in the save file" );
	}

	char tmp1[256];
	long component;
	icomponents.Clear();
	//FitIniFile MtestFile;
	//MtestFile.open("data\\mtest.fit", CREATE);
	//MtestFile.writeIdLong( tmp1, iComponentCount );
	for ( int i = 0; i < iComponentCount; i++ )
	{
		sprintf( tmp1, "IComponent%ld", i );
		//MtestFile.writeBlock( tmp1 );
		
		if ( NO_ERR != file.readIdLong( tmp1, component ) )
			break;

	for ( COMPONENT_LIST::EIterator cIter = components.Begin(); !cIter.IsDone(); cIter++ )
	{
		if ( (*cIter).getID() == component )
		{
			LogisticsComponent* inComp = new LogisticsComponent( (*cIter) );
			icomponents.Append( inComp );
		}
	}
	}

//Magic end
//M18 begin
	//magic 24122010 begin
	//magic 29122010 begin
	if (purMechsCount > 0)
	{
	//magic 29122010 end
		FullPathFileName mFullFileName;
		mFullFileName.init(missionPath, "mechPurchase", ".fit");
		FitIniFile writePurchaseFile;
		writePurchaseFile.open(mFullFileName, CREATE);
		writePurchaseFile.writeBlock( "General" );
		writePurchaseFile.writeIdLong( "PurMechsCount", purMechsCount );
		//magic 24122010 end
		// load purchase mechs inventory
		//int mCount = 0;
		for ( i = 0; i < purMechsCount; i++ )
		{
			sprintf( tmp, "MechPurchase%ld", i ) ;
			if ( NO_ERR != file.seekBlock( tmp ) )
			{
				gosASSERT( 0 );
			}

			//loadPurMech( file, mCount ); //magic disabled 24122010
			//magic 24122010 begin
			writePurchaseFile.writeBlock( tmp );
			char tmp2[256];
			file.readIdString( "Chassis", tmp2, 255 );
			writePurchaseFile.writeIdString( "Chassis", tmp2 );
			file.readIdString( "Variant", tmp2, 255 );
			writePurchaseFile.writeIdString( "Variant", tmp2 );
			long mpq;
			file.readIdLong( "Quantity", mpq );
			writePurchaseFile.writeIdLong( "Quantity", mpq );
		//magic 24122010 end
		}
		//magic 24122010 begin
		writePurchaseFile.close();
		//magic 24122010 end
	//magic 29122010 begin
	}
	//magic 29122010 end
//Magic begin load components inventory


	long result2 = file.seekBlock( "purComponents" );
	if ( result2 != NO_ERR )
	{
		Assert( 0, 0, "No components in the save file" );
	}

	char tmp2[256];
	long pcomponent;
	purcomponents.Clear();
	//FitIniFile MtestFile;
	//MtestFile.open("data\\mtest.fit", CREATE);
	//MtestFile.writeIdLong( tmp1, iComponentCount );
	for ( int i = 0; i < purComponentCount; i++ )
	{
		sprintf( tmp2, "PComponent%ld", i );
		//MtestFile.writeBlock( tmp1 );
		
		if ( NO_ERR != file.readIdLong( tmp2, pcomponent ) )
			break;

	for ( COMPONENT_LIST::EIterator cIter = components.Begin(); !cIter.IsDone(); cIter++ )
	{
		if ( (*cIter).getID() == pcomponent )
		{
			LogisticsComponent* inComp = new LogisticsComponent( (*cIter) );
			purcomponents.Append( inComp );
		}
	}
	}

//M18 end

	updateAvailability(); //Magic disabled


	//Start finding the Leaks
	//systemHeap->dumpRecordLog();
	loadFromSave = true;
	return 0;
}

long LogisticsData::loadVariant( FitIniFile& file )
{
	char tmp[256];
	
	file.readIdString( "Chassis", tmp, 255 );

	const LogisticsChassis* pChassis  = NULL;
	// go out and find that chassis
	for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
	{
		if ( (*vIter)->getFileName().Compare(  tmp, 0 ) == 0 )
		{
			pChassis = (*vIter)->getChassis();
		}
	}

	if ( !pChassis ) // we can always try and make it ourseleves, but it should have been loaded by now
	{
		gosASSERT( 0 );
		return INVALID_FILE_NAME;
	}

	// create the variant, add to the list
	file.readIdString( "VariantName", tmp, 255 );
	LogisticsVariant* pVariant = new LogisticsVariant( pChassis, 0 );	

	variants.Append( pVariant );
	
	pVariant->setName( tmp );
	//Magic 61 begin
	long tmpSensID;
	file.readIdLong( "VariantSensor", tmpSensID );
	pVariant->setSensorID(tmpSensID);
	//Magic 61 end

	long componentCount = 0;
	long x = 0;
	long y = 0;
	long location = 0;
	long id = 0;

	char tmp2[256];
	
	// read number of components
	file.readIdLong( "ComponentCount", componentCount );

	// add those components
	for ( int i = 0; i < componentCount; i++ )
	{
		sprintf( tmp, "Component%ld", i );
		file.readIdLong(tmp, id );
		
		strcpy( tmp2, tmp );
		strcat( tmp2, "x" );
		file.readIdLong( tmp2, x );

		strcpy( tmp2, tmp );
		strcat( tmp2, "y" );
		file.readIdLong( tmp2, y );

		strcpy( tmp2, tmp );
		strcat( tmp2, "Location" );
		file.readIdLong( tmp2, location );

		pVariant->addComponent( id, x, y );
	}
	//magic 12052012 begin
	float curIntPoints[8];
	file.readIdFloat( "CurInternalHead", curIntPoints[0] );
	file.readIdFloat( "CurInternalCTorso", curIntPoints[1] );
	file.readIdFloat( "CurInternalLTorso", curIntPoints[2] );
	file.readIdFloat( "CurInternalRTorso", curIntPoints[3] );
	file.readIdFloat( "CurInternalLArm", curIntPoints[4] );
	file.readIdFloat( "CurInternalRArm", curIntPoints[5] );
	file.readIdFloat( "CurInternalLLeg", curIntPoints[6] );
	file.readIdFloat( "CurInternalRLeg", curIntPoints[7] );
	pVariant->setCurInternalPoints(curIntPoints);

	float curArmPoints[11];
	file.readIdFloat( "CurArmorHead", curArmPoints[0] );
	file.readIdFloat( "CurArmorCTorso", curArmPoints[1] );
	file.readIdFloat( "CurArmorLTorso", curArmPoints[2] );
	file.readIdFloat( "CurArmorRTorso", curArmPoints[3] );
	file.readIdFloat( "CurArmorLArm", curArmPoints[4] );
	file.readIdFloat( "CurArmorRArm", curArmPoints[5] );
	file.readIdFloat( "CurArmorLLeg", curArmPoints[6] );
	file.readIdFloat( "CurArmorRLeg", curArmPoints[7] );
	file.readIdFloat( "CurArmorRCTorso", curArmPoints[8] );
	file.readIdFloat( "CurArmorRLTorso", curArmPoints[9] );
	file.readIdFloat( "CurArmorRRTorso", curArmPoints[10] );
	pVariant->loadCurArmorPoints(curArmPoints);
	//magic 12052012 end
	return 0;
}

long LogisticsData::loadMech( FitIniFile& file, int& count )
{
	char tmp[256];
	file.readIdString( "Variant", tmp, 255 );
	for ( VARIANT_LIST::EIterator mIter = variants.Begin(); !mIter.IsDone(); mIter++ )
	{
		if ( (*mIter)->getName().Compare( tmp, 0 ) == 0 )
		{
			
			LogisticsMech* pMech = new LogisticsMech( (*mIter), count );
			file.readIdString( "Pilot", tmp, 255 );
			inventory.Append( pMech );

			for ( PILOT_LIST::EIterator pIter = pilots.Begin(); !pIter.IsDone(); pIter++ )
			{
				if ( (*pIter).getFileName().Compare( tmp, 0  ) == 0 )
				{
					pMech->setPilot( &(*pIter ) );
					count++;
					if ( count > -1 && count < 13 )
						pMech->setForceGroup( count );
					break;
				}
			}
			//magic 05052012 begin
			float curInteranalPoints[8];
			file.readIdFloat( "CurInternalHead", curInteranalPoints[0] );
			file.readIdFloat( "CurInternalCTorso", curInteranalPoints[1] );
			file.readIdFloat( "CurInternalLTorso", curInteranalPoints[2] );
			file.readIdFloat( "CurInternalRTorso", curInteranalPoints[3] );
			file.readIdFloat( "CurInternalLArm", curInteranalPoints[4] );
			file.readIdFloat( "CurInternalRArm", curInteranalPoints[5] );
			file.readIdFloat( "CurInternalLLeg", curInteranalPoints[6] );
			file.readIdFloat( "CurInternalRLeg", curInteranalPoints[7] );
			pMech->setCurInternalArray(curInteranalPoints);

			float curArmorPoints[11];
			file.readIdFloat( "CurArmorHead", curArmorPoints[0] );
			file.readIdFloat( "CurArmorCTorso", curArmorPoints[1] );
			file.readIdFloat( "CurArmorLTorso", curArmorPoints[2] );
			file.readIdFloat( "CurArmorRTorso", curArmorPoints[3] );
			file.readIdFloat( "CurArmorLArm", curArmorPoints[4] );
			file.readIdFloat( "CurArmorRArm", curArmorPoints[5] );
			file.readIdFloat( "CurArmorLLeg", curArmorPoints[6] );
			file.readIdFloat( "CurArmorRLeg", curArmorPoints[7] );
			file.readIdFloat( "CurArmorRCTorso", curArmorPoints[8] );
			file.readIdFloat( "CurArmorRLTorso", curArmorPoints[9] );
			file.readIdFloat( "CurArmorRRTorso", curArmorPoints[10] );
			pMech->loadCurArmorArray(curArmorPoints);
			//magic 05052012 end

			// it could have had no pilot
			return 0;
		}
	}

	return -1; // failed in finding the variant
}



void	LogisticsData::setMissionCompleted( )
{
#ifndef VIEWER
	const char* pMissionName = missionInfo->getCurrentMission();
	missionInfo->setMissionComplete();

	rpJustAdded = 0;

	// first set all pilots as not just dead
	for ( PILOT_LIST::EIterator iter = pilots.Begin(); !iter.IsDone();
		iter++ )
		{
			(*iter).setUsed( 0 );
		}

	for ( EList< CObjective*, CObjective* >::EIterator oIter =  Team::home->objectives.Begin();
			!oIter.IsDone(); oIter++ )
	{
		if ( (*oIter)->Status(Team::home->objectives) == OS_SUCCESSFUL )
		{
			addCBills( (*oIter)->ResourcePoints() );
		}
	}

	// need to go find out which pilots died.
	Team* pTeam = Team::home;

	int ForceGroupCount = 1;

	if ( pTeam )
	{
		for ( int i = pTeam->getRosterSize() - 1; i > -1; i-- )
		{
			Mover* pMover = (Mover*)pTeam->getMover( i );
			
			//Must check if we ever linked up with the mech!!
			if ( pMover->isOnGUI() && 
				 (pMover->getObjectType()->getObjectTypeClass() == BATTLEMECH_TYPE) && 
				 (pMover->getCommanderId() == Commander::home->getId()) &&
				 (pMover->getMoveType() != MOVETYPE_AIR))
			{
				LogisticsMech* pMech = getMech( pMover->getName(), pMover->getPilot()->getName() );

				unsigned long base, highlight1, highlight2;
				((Mech3DAppearance*)pMover->getAppearance())->getPaintScheme( base, highlight1, highlight2 );
				LogisticsPilot* pPilot = getPilot( pMover->getPilot()->getName() );
				if ( pMech )
				{
					if ( pMover->isDestroyed() || pMover->isDisabled() )
					{
						removeMechFromInventory( pMech->getName(), pMover->getPilot()->getName() );
					}
					else
					{
						removeMechFromInventory( pMech->getName(), pMover->getPilot()->getName() );
						LogisticsVariant* pVar = getVariant( ((BattleMech*)pMover)->variantName );
						//magic 06052012 begin
							float curMoverArmor[11] = {0,0,0,0,0,0,0,0,0,0,0};

							for (int j=0; j<11; j++)
							{
								//curMoverArmor[j] = pMover->armor[j].maxArmor - pMover->armor[j].curArmor;
								curMoverArmor[j] = pMover->armor[j].curArmor; //magic 28092012
							}
							//pVar->setCurArmorPoints(curMoverArmor);
							pVar->loadCurArmorPoints(curMoverArmor); //magic 28092012

							float curMoverInternal[8];
							for (int k=0; k<8; k++)
								curMoverInternal[k] = pMover->body[k].curInternalStructure;

							pVar->setCurInternalPoints(curMoverInternal);
						//magic 06052012 end
						addMechToInventory( pVar, ForceGroupCount++, pPilot, base, highlight1, highlight2 );
					
						/*//magic 12052012 begin
						//removeMechFromInventory( pMech->getName(), pMover->getPilot()->getName() );
						//LogisticsVariant* pVar = getVariant( ((BattleMech*)pMover)->variantName );
					
							float curMoverArmor[11] = {0,0,0,0,0,0,0,0,0,0,0};

							for (int j=0; j<11; j++)
							{
								curMoverArmor[j] = pMover->armor[j].maxArmor - pMover->armor[j].curArmor;

							}
							pMech->setCurArmorArray(curMoverArmor);

							float curMoverInternal[8];
							for (int k=0; k<8; k++)
								curMoverInternal[k] = pMover->body[k].curInternalStructure;

							pMech->setCurInternalArray(curMoverInternal);

						//addMechToInventory( pVar, ForceGroupCount++, pPilot, base, highlight1, highlight2 );
						//magic 12052012 end*/
						//magic 13052012 begin
						if (pVar->getInternalDamage() > 0)
						{
							ForceGroupCount--;
							if ( pPilot )
								pPilot->setUsed( false );
						}
						else
						//magic 13052012 end*/
						if ( pPilot )
							pPilot->setUsed( true );

						//magic 13052012 begin
						pVar->maximizeArmorPoints();
						pVar->maximizeInternalPoints();
						//magic 13052012 end*/

					}
				}
				else // mech was recovered during the mission
				{
					if ( !pMover->isDestroyed() && !pMover->isDisabled() )
					{
						// find the variant with this mech's info
					LogisticsVariant* pVariant = getVariant( ((BattleMech*)pMover)->variantName );
						if ( !pVariant )
						{
							Assert( 0, 0, "couldn't find the variant of a salvaged mech" );
						}
						else
						{
						//magic 06052012 begin
							float curMoverArmor[11] = {0,0,0,0,0,0,0,0,0,0,0};

							for (int j=0; j<11; j++)
							{
								curMoverArmor[j] = pMover->armor[j].maxArmor - pMover->armor[j].curArmor;

							}
							pVariant->setCurArmorPoints(curMoverArmor);

							float curMoverInternal[8];
							for (int k=0; k<8; k++)
								curMoverInternal[k] = pMover->body[k].curInternalStructure;

							pVariant->setCurInternalPoints(curMoverInternal);
						//magic 06052012 end
							addMechToInventory( pVariant, ForceGroupCount++, pPilot, base, highlight1, highlight2 );

							//magic 13052012 begin
							if (pVariant->getInternalDamage() > 0)
							{
								ForceGroupCount--;
								if ( pPilot )
									pPilot->setUsed( false );
							}
							else
							//magic 13052012 end*/
							if ( pPilot )
								pPilot->setUsed( true );

							//magic 12052012 begin
							pVariant->maximizeArmorPoints();
							pVariant->maximizeInternalPoints();
							//magic 12052012 end

						}
					}
				}

  				if ( pPilot )
				{
					pPilot->update( pMover->getPilot() );
//					if ( pMover->isDestroyed() || pMover->isDisabled() )
//						pPilot->setUsed( false );

				}
			}
		}
		//magic 04092012 begin
		// ovde ubaciti salvage
		for ( ICOMPONENT_LIST::EIterator cIter = salvageComponents.Begin(); !cIter.IsDone(); cIter++ )
		{

				LogisticsComponent* inComp = new LogisticsComponent( **cIter );
				icomponents.Append( inComp ); //OK but dont want to append to inventory before mission success
				//salvageComponents.Append( inComp );
		
		}
		salvageComponents.Clear();
		//magic 04092012 end
	}

#endif
}

LogisticsMech*  LogisticsData::getMech( const char* MechName, const char* pilotName )
{
	for( MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getName().Compare( MechName, 0 ) == 0 )
		{
			if ( !pilotName )
			{
				if ( !(*iter)->getPilot() )
					return (*iter);
			}

			else
			{
				if ( (*iter)->getPilot() && (*iter)->getPilot()->getName().Compare( pilotName, 0 ) == 0 )
					return (*iter);
			}
		}
	}

	return NULL;
}

void LogisticsData::removeMechFromInventory( const char* mechName, const char* pilotName )
{
	LogisticsMech* pMech = getMech( mechName, pilotName );

	gosASSERT( pMech );

	if ( pMech )
	{
		inventory.Delete( inventory.Find(pMech) );
		delete pMech;
	}
}

LogisticsPilot*	LogisticsData::getPilot( const char* pilotName )
{
	// look for available ones first
	for( PILOT_LIST::EIterator iter = pilots.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter).isAvailable() )
		{
			if ( (*iter).getName().Compare( pilotName, 0 ) == 0 )
			{
				return &(*iter);
			}
		}
	}
	
	for( iter = pilots.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter).getName().Compare( pilotName, 0 ) == 0 )
		{
			return &(*iter);
		}
	}

	return NULL;
}

LogisticsVariant* LogisticsData::getVariant( const char* mechName )
{
	for ( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getName().Compare( mechName, 0 ) == 0 )
			return (*iter);
	}

	return NULL;
}

long LogisticsData::updateAvailability()
{
	bNewWeapons = 0;
	EString purchaseFileName = missionInfo->getCurrentPurchaseFile();
	purchaseFileName.MakeLower();

	if ( purchaseFileName.Length() < 1 )
	{
		purchaseFileName = missionPath;
		purchaseFileName += "purchase.fit";
	}

	int oldMechAvailableCount= 0;
	int newMechAvailableCount = 0;
	int oldPilotAvailableCount = 0;
	int newPilotAvailableCount= 0;
	for ( PILOT_LIST::EIterator pIter = pilots.Begin(); !pIter.IsDone(); pIter++ )
	{
		if ( (*pIter).bAvailable )
			oldPilotAvailableCount++;

		(*pIter).setAvailable( 0 );
	}


	// make sure its around and you can open it 
	FitIniFile file;
	if ( NO_ERR != file.open( (char*)(const char*)purchaseFileName ) )
	{
		EString error;
		error.Format( "Couldn't open %s", (char*)(const char*)purchaseFileName );
		PAUSE(((char*)(const char*)error ));
		return NO_PURCHASE_FILE;
	}
	// read in available components
	bool available[255];
	memset( available, 0, sizeof( bool ) * 255 );
	
	/*Magic Begin disabled 26072010
	long result = file.seekBlock( "Components" );
	if ( result != NO_ERR )
	{
		Assert( 0, 0, "No components in the purchase file" );
	}

	char tmp[32];
	long component;

	bool bAll = 0;
	file.readIdBoolean( "AllComponents", bAll );
	for ( int i = 0; i < 255; i++ )
	{
		if ( bAll )
			available[i] = 1;
		else
		{
			sprintf( tmp, "Component%ld", i );
			if ( NO_ERR != file.readIdLong( tmp, component ) )


				break;

			available[component] = 1;
		}


	}
*/ //Magic end
// Magic begin
		bool bAll = 0;
		char tmp[32];
		//long component;
		for (ICOMPONENT_LIST::EIterator cIter = icomponents.Begin(); !cIter.IsDone(); cIter++)
			available[(*cIter)->getID()] = 1;

// Magic end
	// go through comonent list, and set 'em
	for ( COMPONENT_LIST::EIterator cIter = components.Begin(); !cIter.IsDone(); cIter++ )
	{
		(*cIter).setAvailable( 1 );
/*		if ( available[(*cIter).getID()] || bAll )
		{
			//if ( !(*cIter).isAvailable() )
			//	bNewWeapons = true;
			(*cIter).setAvailable( 1 );
		}*/
	}

	const char* pFileNames[512];
	long count = 512;
 	missionInfo->getAdditionalPurachaseFiles( pFileNames, count );


	// reset all variants to unavailable
	for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
	{
		if ( (*vIter)->isAvailable()  && !((*vIter)->getID() >> 16 ))
			oldMechAvailableCount++;
		(*vIter)->setAvailable(0);
	}

	for ( int i = 0; i < count; i++ )
	{
		appendAvailability( pFileNames[i], available );
	}

	// go through comonent list, and set 'em
	for ( cIter = components.Begin(); !cIter.IsDone(); cIter++ )
	{
		if ( !available[(*cIter).getID()]  )
		{
			(*cIter).setAvailable( 0 );
		}
	}



	// go through each variant, and see if it's available
	char chassisFileName[255];
	long componentArray[255];
	long componentCount;

	file.seekBlock( "Mechs" );
	for ( int i = 0; i < 255; i++ )
	{
		sprintf( tmp, "Mech%ld", i );
		if ( NO_ERR != file.readIdString( tmp, chassisFileName, 254 ) )
			break;

		// go through each variant, if it has the same chassis, check and see if all of its components are valid
		for ( vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
		{
			EString mechName = (*vIter)->getFileName();
			char realName[1024];
			_splitpath( mechName, NULL, NULL, realName, NULL );
			if ( stricmp( realName, chassisFileName ) == 0 )
			{
				componentCount = 255;
				bool bRight = true;
				(*vIter)->getComponents( componentCount, componentArray );
	/*M23			for ( int i = 0; i < componentCount; i++ )
				{
					if ( !available[componentArray[i]] ) // unavailable componets
					{
						//char errorStr[256];
						//sprintf( errorStr, "mech %s discarded because it contains a %ld", 
						//	chassisFileName, componentArray[i] );
						//PAUSE(( errorStr ));
						bRight= false;
						break;
					}
				}
*/ //M23
				if ( bRight )
				{
					(*vIter)->setAvailable( true );

					if (  !((*vIter)->getID() >> 16 ) )
						newMechAvailableCount++;
				}
			}
		}
	}

	if ( newMechAvailableCount != oldMechAvailableCount )
		bNewMechs = true;
	else
		bNewMechs = false;

	// add new pilots
	char pilotName[255];
	file.seekBlock( "Pilots" );
	for ( int i = 0; i < 255; i++ )
	{
		sprintf( tmp, "Pilot%ld", i );
		if ( NO_ERR != file.readIdString( tmp, pilotName, 254 ) )
			break;

		for ( PILOT_LIST::EIterator pIter = pilots.Begin(); !pIter.IsDone(); pIter++ )
		{
			if ( (*pIter).getFileName().Compare( pilotName, 0 ) == 0 )
			{
				(*pIter).setAvailable( true );
			}

		}
	}

	for ( pIter = pilots.Begin(); !pIter.IsDone(); pIter++ )
	{
		
		if ( (*pIter).bAvailable )
			newPilotAvailableCount++;
	}


	if ( oldPilotAvailableCount != newPilotAvailableCount && newPilotAvailableCount > oldPilotAvailableCount )
		bNewPilots = true;
	else
		bNewPilots = 0;



	return 0;

}

void LogisticsData::appendAvailability(const char* pFileName, bool* availableArray )
{
	FitIniFile file;
	if ( NO_ERR != file.open( pFileName ) )
	{
		return;
	}
		bool bAll = 0;
		//char tmp[32];
		//long component;
		for (ICOMPONENT_LIST::EIterator cIter = icomponents.Begin(); !cIter.IsDone(); cIter++)
		{			
			//available[(*cIter)->getID()] = 1;
			LogisticsComponent* pComp = getComponent( (*cIter)->getID() );
			pComp->setAvailable( true );
		}
	

	// add new pilots
	char pilotName[255];
	char tmp[256];
	file.seekBlock( "Pilots" );
	for ( int i = 0; i < 255; i++ )
	{
		sprintf( tmp, "Pilot%ld", i );
		if ( NO_ERR != file.readIdString( tmp, pilotName, 254 ) )
			break;

		for ( PILOT_LIST::EIterator pIter = pilots.Begin(); !pIter.IsDone(); pIter++ )
		{
			if ( (*pIter).getFileName().Compare( pilotName, 0 ) == 0 )
			{
				(*pIter).setAvailable( true );
				bNewPilots = true;
				
			}

		}
	}

	file.seekBlock( "Mechs" );
	int newAvailableCount = 0;
	char chassisFileName[256];
	for ( i = 0; i < 255; i++ )
	{
		sprintf( tmp, "Mech%ld", i );
		if ( NO_ERR != file.readIdString( tmp, chassisFileName, 255 ) )
			break;

		// go through each variant, if it has the same chassis, check and see if all of its components are valid
		for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
		{
			EString mechName = (*vIter)->getFileName();
			char realName[255];
			_splitpath( mechName, NULL, NULL, realName, NULL );
			if ( stricmp( realName, chassisFileName ) == 0 )
			{
				long componentCount = 255;
				long componentArray[256];
				bool bRight = true;
				(*vIter)->getComponents( componentCount, componentArray );
/*M23				for ( int i = 0; i < componentCount; i++ )
				{
					LogisticsComponent* pComp = getComponent(componentArray[i]);
					if ( !pComp->isAvailable() ) // unavailable componets
					{
						//char errorStr[256];
						//sprintf( errorStr, "mech %s discarded because it contains a %ld", 
						//	chassisFileName, componentArray[i] );
						//PAUSE(( errorStr ));
						bRight= false;
						break;
					}
				}
*/ //M23
				if ( bRight )
				{
					(*vIter)->setAvailable( true );
					newAvailableCount++;
				}

				break;
			}
		}
	}
}

const EString& LogisticsData::getCurrentMission() const
{
	return missionInfo->getCurrentMission(); 
}

const EString& LogisticsData::getLastMission() const
{
	return missionInfo->getLastMission(); 
}

const char * LogisticsData::getCurrentABLScript() const
{
	return missionInfo->getCurrentABLScriptName();
}

long LogisticsData::getCurrentMissionTune()
{
	return missionInfo->getCurrentLogisticsTuneId();
}

long LogisticsData::getCurrentMissionId()
{
	return missionInfo->getCurrentMissionId();
}

void LogisticsData::clearInventory()
{
	for (MECH_LIST::EIterator iter = inventory.Begin(); !iter.IsDone(); iter++ )
	{
		(*iter)->setPilot( NULL );
		delete *iter;
	}

	inventory.Clear();
}

int	LogisticsData::getPilotCount()
{
	return pilots.Count();
}
int	LogisticsData::getPilots( LogisticsPilot** pArray, long& count )
{
	if ( count < pilots.Count() )
	 {
		return NEED_BIGGER_ARRAY;
	 }	 

	 count= 0;

	 for ( PILOT_LIST::EIterator iter = instance->pilots.Begin(); !iter.IsDone(); iter++  )
	 {
		 pArray[count++] = &(*iter);
	 }

	 return 0;
}

int LogisticsData::getMaxDropWeight() const
{
	return  missionInfo->getCurrentDropWeight(); //M17
	//return  missionInfo->getTopDropWeight(); //M17
}
//magic 02062012 begin
int LogisticsData::getCurrentRP() const
{
	return  missionInfo->getCurrentRP();
}
//magic 02062012 end


int LogisticsData::getCurrentDropWeight() const
{
	long retVal = 0;
	for ( MECH_LIST::EIterator iter = instance->inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getForceGroup() )
		{
			retVal += (*iter)->getMaxWeight();
		}

	}

	return retVal;

}

bool	LogisticsData::canAddMechToForceGroup( LogisticsMech* pMech )
{
	if ( !pMech )
		return 0;


	int maxUnits = 12;

#ifndef VIEWER
	if ( MPlayer )
	{
		long playerCount;
		MPlayer->getPlayers( playerCount );
		maxUnits = MAX_MULTIPLAYER_MECHS_IN_LOGISTICS/playerCount;

		if ( maxUnits > 12 )
			maxUnits = 12;
	}
#endif

	int fgCount = 0;
	
	for ( MECH_LIST::EIterator iter = instance->inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getForceGroup() )
			{
				fgCount ++;
			}


	}

	if ( fgCount >= maxUnits )
		return 0;


	return (pMech->getMaxWeight() + getCurrentDropWeight() <= getMaxDropWeight() ) ? 1 : 0;
}


int LogisticsData::getVariantsInInventory( LogisticsVariant* pVar, bool bIncludeForceGroup )
{
	long retVal = 0;
	for ( MECH_LIST::EIterator iter = instance->inventory.Begin(); !iter.IsDone(); iter++ )
	{
		if ( (*iter)->getVariant() == pVar )
		{
			if ( !(*iter)->getForceGroup() || bIncludeForceGroup )
			{
				retVal ++;
			}
		}

	}

	return retVal;

}

int		LogisticsData::getChassisVariants( const LogisticsChassis* pChassis, 
										  LogisticsVariant** pVar, 
										  int& maxCount )
{
	int retVal = 0;
	
	int i = 0;
	for ( VARIANT_LIST::EIterator iter = variants.Begin(); 
		!iter.IsDone(); iter++ )
		{
			if ( (*iter)->getChassis() == pChassis  )
			{
				if ( i < maxCount )
					pVar[i]	= (*iter);	
				else 
					retVal = NEED_BIGGER_ARRAY;
				++i;

				
			} 
		}

	maxCount = i;

	return retVal; 
}


int LogisticsData::setMechToModify( LogisticsMech* pMech )
{
	if ( !pMech )
		return -1;

	currentlyModifiedMech = pMech;
	oldVariant = pMech->getVariant();

	LogisticsVariant* pVar = new LogisticsVariant( *oldVariant );
	pMech->setVariant( pVar );

	return 0;
}

void encryptFile (char *inputFile, char* outputFile)
{
	//Now we encrypt this by zlib Compressing the file passed in.
	// Then LZ Compressing the resulting zlib data.
	// Since our LZ compression is pretty much non-standard, that should be enough.
	MemoryPtr rawData = NULL;
	MemoryPtr zlibData = NULL;
	MemoryPtr LZData = NULL;

	File dataFile;
	dataFile.open(inputFile);
	DWORD fileSize = dataFile.fileSize();
	rawData = (MemoryPtr)malloc(fileSize);
	zlibData = (MemoryPtr)malloc(fileSize*2);
	LZData = (MemoryPtr)malloc(fileSize*2);

	dataFile.read(rawData,fileSize);

	DWORD zlibSize = fileSize * 2;
	compress2(zlibData,&zlibSize,rawData,fileSize,0);
	DWORD lzSize = LZCompress (LZData, zlibData, zlibSize);

	dataFile.close();

	File binFile;
	binFile.create(outputFile);
	binFile.writeLong(lzSize);
	binFile.writeLong(zlibSize);
	binFile.writeLong(fileSize);
	binFile.write(LZData,lzSize);
	binFile.close();

	free(rawData);
	free(zlibData);
	free(LZData);
}

void decryptFile (char *inputFile, char *outputFile)
{
	//Now we decrypt this by lz deCompressing the zlib file created.
	// Then zlib deCompressing the resulting zlib data into the raw File again.
	// Since our LZ compression is pretty much non-standard, that should be enough.
	MemoryPtr rawData = NULL;
	MemoryPtr zlibData = NULL;
	MemoryPtr LZData = NULL;

	File dataFile;
	long result = dataFile.open(inputFile);
	if (result == NO_ERR) 
	{
		DWORD lzSize = dataFile.readLong();
		DWORD zlibSize = dataFile.readLong();
		DWORD fileSize = dataFile.readLong();
	
		rawData = (MemoryPtr)malloc(fileSize);
		zlibData = (MemoryPtr)malloc(zlibSize);
		LZData = (MemoryPtr)malloc(lzSize);
	
		dataFile.read(LZData,lzSize);
	
		DWORD testSize = fileSize;
		DWORD test2Size = LZDecomp(zlibData, LZData, lzSize);
		if (test2Size != zlibSize) 
			STOP(("Didn't Decompress to same size as started with!!"));
	
		uncompress((MemoryPtr)rawData,&testSize,zlibData,zlibSize);
		if (testSize != fileSize) 
			STOP(("Didn't Decompress to correct format"));
	
		dataFile.close();
	
		File binFile;
		binFile.create(outputFile);
		binFile.write(rawData,fileSize);
		binFile.close();
	
		free(rawData);
		free(zlibData);
		free(LZData);
	}
}

int LogisticsData::acceptMechModifications( const char* name )
{
	if ( !currentlyModifiedMech )
		return -1;

	bool bFound = 0;
	//if ( oldVariant )
	//{
	//	missionInfo->incrementCBills( oldVariant->getCost() );
	//}
	if ( oldVariant && oldVariant->isDesignerMech() )
	{
		bFound = 1;
	}
	else
	{
		for ( MECH_LIST::EIterator iter = inventory.Begin();
			!iter.IsDone(); iter++ )
		{
			if ( (*iter)->getVariant() == oldVariant && (*iter) != currentlyModifiedMech  )
			{
				bFound = 1;
			}
		}
	}

	if ( !bFound )
	{
		VARIANT_LIST::EIterator vIter = variants.Find( oldVariant );
		if ( vIter != VARIANT_LIST::INVALID_ITERATOR
			&& oldVariant->getName().Compare( name ) == 0 )
		{
			variants.Delete( vIter );
			delete oldVariant;
		}
	}

	// now need to get rid of variants with this name....

	//Code added by Frank on  May 3, 2001 @ 9:54pm.
	// If you run this with the compare set to vIter.IsDone, it crashes in Profile
	// based on Sean Bug number 4359.  We traverse past end of list and crash.
	// Doing it by count does not crash and has the added advantage of being easy to debug!
	// I suspect ESI going south again.  Probably a compiler option...

	// 05/04 HKG, actually, if you increment vIter after deleteing it, it still won't work

	// Good Point.  As you can see, it was pretty late when I "fixed" this!
	long numVariants = variants.Count();
	long i=0;
	for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone();  )
	{
		if ( (*vIter)->getName().Compare( name, 0 ) == 0 )
		{
			VARIANT_LIST::EIterator tmpIter = vIter;
			vIter++;
			delete (*tmpIter);
			variants.Delete( tmpIter );
		}
		else
			vIter++;
	}

	currentlyModifiedMech->getVariant()->setName( name );
	//magic 12052012 begin
	float* curArm = currentlyModifiedMech->getCurArmorArray();
	currentlyModifiedMech->getVariant()->loadCurArmorPoints(curArm);
	float* curInt = currentlyModifiedMech->getCurInternalArray();
	currentlyModifiedMech->getVariant()->setCurInternalPoints(curInt);
	//magic 12052012 end
	variants.Append( currentlyModifiedMech->getVariant() );
	//missionInfo->decrementCBills( currentlyModifiedMech->getVariant()->getCost() );

	currentlyModifiedMech = 0;
	oldVariant = 0;


	// temporary, looking for dangling pointers
	for ( MECH_LIST::EIterator iter = inventory.Begin();
		!iter.IsDone(); iter++ )
	{
		if ( (*iter)->getVariant()->getCost() )
		{
			bFound = 1;
		}
	}
 
#ifndef VIEWER

	if ( MPlayer )
	{
		// save the player created variants
		for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone();  vIter++ )
		{
			if ( !(*vIter)->isDesignerMech() )
			{
				FullPathFileName mechFile;
				mechFile.init("data\\multiplayer\\",(*vIter)->getName(),".var");

				FitIniFile file;
				file.create(mechFile);
				(*vIter)->save( file, 0 );
				file.close();

				encryptFile(mechFile,mechFile);
			}
		}
	}

#endif

	return 0;
}
int LogisticsData::acceptMechModificationsUseOldVariant( const char* name )
{
	if ( !currentlyModifiedMech )
		return -1;

	//if ( oldVariant )
	//{
	//	missionInfo->incrementCBills( oldVariant->getCost() );
	//}

	LogisticsVariant* pVar = getVariant( name );

	if ( !pVar )
		Assert( 0, 0, "couldn't find the old variant\n" );

	LogisticsVariant* pOldVar = currentlyModifiedMech->getVariant();
	delete pOldVar;

	currentlyModifiedMech->setVariant( pVar );
	//missionInfo->decrementCBills( pVar->getCost() );
	
	currentlyModifiedMech = 0;
	oldVariant = 0;	

	return 0;
}

bool LogisticsData::canReplaceVariant( const char* name )
{
	int nameCount = 0;
	for ( MECH_LIST::EIterator iter = inventory.Begin();
		!iter.IsDone(); iter++ )
		{
			if ( (*iter)->getName().Compare( name, 0 ) == 0 )
			{
				nameCount++;
				if ( (*iter)->getVariant() != oldVariant && (*iter) != currentlyModifiedMech )
				{
					return 0;
				}
			}
		}

		if ( nameCount > 1 )
			return 0;

		for ( VARIANT_LIST::EIterator vIter = variants.Begin();
		!vIter.IsDone(); vIter++ )
		{
			if ( (*vIter)->isDesignerMech() && (*vIter)->getName().Compare( name, 0 ) == 0 )
				return 0;
		}

		return true;
}

bool	LogisticsData::canDeleteVariant( const char* name )
{
	LogisticsVariant* pVariant = getVariant( name );
	if ( !pVariant )
		return 0;
		
	if ( !canReplaceVariant(name) )
		return 0;

	if ( currentlyModifiedMech->getName() == name || oldVariant->getName() == name )
		return 0;

	return 1;

}

int LogisticsData::cancelMechModfications()
{
	if ( !currentlyModifiedMech )
		return -1;

	LogisticsVariant* pCancel = currentlyModifiedMech->getVariant();

	delete pCancel;

	currentlyModifiedMech->setVariant( oldVariant );

	oldVariant = 0;
	currentlyModifiedMech = 0;

	return 0;
}

const char*			LogisticsData::getCurrentOperationFileName()
{
	return missionInfo->getCurrentOperationFile();
}
const char*			LogisticsData::getCurrentVideoFileName()
{
	return missionInfo->getCurrentVideo();
}

const char*			LogisticsData::getCurrentMissionDescription()
{
	return missionInfo->getCurrentMissionDescription();
}


const char*				LogisticsData::getCurrentMissionFriendlyName( )
{
	return missionInfo->getCurrentMissionFriendlyName();
}

const char*				LogisticsData::getMissionFriendlyName( const char* missionName )
{
	return missionInfo->getMissionFriendlyName( missionName );
}

/*long				LogisticsData::getMaxTeams() const
{
/	return missionInfo->getMaxTeams( );
}*/


void				LogisticsData::startNewCampaign( const char* fileName )
{
#ifndef VIEWER
	if ( MPlayer )
	{
		delete MPlayer;
		MPlayer = NULL;
	}
#endif
	inventory.Clear();
	icomponents.Clear(); //Magic 61 activated
	purcomponents.Clear(); //Magic 61
	purvariants.Clear(); //Magic 61
	salvageComponents.Clear(); //magic 04092012

	resourcePoints = 0;
	pilots.Clear();
	initPilots();

	FitIniFile file;

	FullPathFileName path;
	path.init( campaignPath, fileName, ".fit" );
	if ( NO_ERR != file.open( path ) )
	{
		STOP(("COuld not find file %s to load campaign",path));
	}
	//magic 23122010 begin
	FullPathFileName mFullFileName;
	mFullFileName.init(missionPath, "mechPurchase", ".fit");
	remove(mFullFileName);
	//magic 23122010 end

	missionInfo->init( file );

	// temporary, just so we can test
	int count = 32;
	const char* missionNames[32];
	missionInfo->getAvailableMissions( missionNames, count );

	setCurrentMission( missionNames[0] );

	soundSystem->setMusicVolume( prefs.MusicVolume );
	//soundSystem->playDigitalMusic(missionInfo->getCurrentLogisticsTuneId());
}

void LogisticsData::startMultiPlayer()
{
	inventory.Clear();
	resourcePoints = 0;
	pilots.Clear();
	initPilots();

	// kill all old designer mechs
	clearVariants();

	// need to initialize multiplayer variants here...
	char findString[512];
	sprintf(findString,"data\\multiplayer\\*.var");

	WIN32_FIND_DATA	findResult;
	HANDLE searchHandle = FindFirstFile(findString,&findResult); 
	if (searchHandle != INVALID_HANDLE_VALUE)
	{
		do
		{
			if ((findResult.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				FullPathFileName path;
				path.init("data\\multiplayer\\",findResult.cFileName,"");
				decryptFile(path,"tmp.fit");

				FitIniFile file;
				file.open("tmp.fit");
				file.seekBlock( "Variant0" );

				loadVariant(file);
				file.close();

				//DeleteFile("tmp.fit"); //magic 20022012 disabled
			}
		} while (FindNextFile(searchHandle,&findResult) != 0);

		FindClose(searchHandle);
	}

	missionInfo->setMultiplayer();

#ifndef VIEWER
	if ( !MPlayer )
	{
		MPlayer = new MultiPlayer;
		MPlayer->setup();

		if ( !strlen( &prefs.playerName[0][0] ) )
		{
			cLoadString( IDS_UNNAMED_PLAYER, &prefs.playerName[0][0], 255 );
		}

		ChatWindow::init();
	}
#endif

}
void				LogisticsData::setPurchaseFile( const char* fileName )
{
	missionInfo->setPurchaseFile( fileName );
	if ( MPlayer )
		clearInventory();
		//icomponents.Clear();//Magic 61
	updateAvailability();

}


int					LogisticsData::getCBills() 
{ 
	return missionInfo->getCBills(); 
}
void				LogisticsData::addCBills( int amount )
{ 
	missionInfo->incrementCBills(amount); 
}
void				LogisticsData::decrementCBills( int amount ) 
{ 
	missionInfo->decrementCBills(amount); 
}

int					LogisticsData::getPlayerVariantNames( const char** array, int& count )
{
	int maxCount = count;
	count = 0;

	int retVal = 0;
	for ( VARIANT_LIST::EIterator iter = variants.Begin(); 
		!iter.IsDone(); iter++ )
		{
			if ( !(*iter)->isDesignerMech() )
			{
				if ( count < maxCount )
				{
					array[count] = (*iter)->getName();
				}
				else
				{
					retVal = NEED_BIGGER_ARRAY;
				}
				count++;
			}
		}

		return retVal;
}

int		LogisticsData::getEncyclopediaMechs( const LogisticsVariant** pChassis, int& count )
{
	int retVal = 0;
	int maxCount = count;
	count = 0;
	for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
	{
		if ( (*vIter)->getName().Find( "Prime" ) != -1  && (*vIter)->isDesignerMech() )
		{
			if ( count < maxCount )
				pChassis[count] = (*vIter);
			else 
				retVal = NEED_BIGGER_ARRAY;

			count++;

		}

	}

	return retVal;
}

int	LogisticsData::getHelicopters( const LogisticsVariant** pChassis, int& count )
{
	int retVal = 0;
	int maxCount = count;
	count = 0;
	for ( VARIANT_LIST::EIterator vIter = variants.Begin(); !vIter.IsDone(); vIter++ )
	{
		if ( (((*vIter)->getVariantID() >> 16 & 0xff) == 0) && 
			(*vIter)->getName().Find( "Prime" ) == -1  && (*vIter)->isDesignerMech() )
		{
			if ( count < maxCount )
				pChassis[count] = (*vIter);
			else 
				retVal = NEED_BIGGER_ARRAY;

			count++;

		}

	}

	return retVal;
}


int		LogisticsData::getVehicles( const LogisticsVehicle** pChassis, int& count )
{
	int retVal = 0;
	int maxCount = count;
	count = 0;
	for ( VEHICLE_LIST::EIterator vIter = vehicles.Begin(); !vIter.IsDone(); vIter++ )
	{
			if ( count < maxCount )
				pChassis[count] = (*vIter);
			else 
				retVal = NEED_BIGGER_ARRAY;

			count++;

		

	}

	return retVal;
}


LogisticsVehicle*	LogisticsData::getVehicle( const char* pName )
{
	char tmpStr[256];
	for ( VEHICLE_LIST::EIterator vIter = vehicles.Begin(); !vIter.IsDone(); vIter++ )
	{
		cLoadString( (*vIter)->getNameID(), tmpStr, 255 );
		if ( stricmp( tmpStr, pName ) == 0 )
		{
			return *vIter;
		}
	}

	return NULL;
}

int LogisticsData::addBuilding( long fitID, PacketFile& objectFile, float scale )
{
	if ( NO_ERR != objectFile.seekPacket(fitID) )
		return -1;

	int fileSize = objectFile.getPacketSize();

	if ( fileSize )
	{
		Building bldg;

		FitIniFile file;
		file.open(&objectFile, fileSize);
		if ( NO_ERR != file.seekBlock( "ObjectType" ) )
			gosASSERT( 0 );

		file.readIdString( "AppearanceName", bldg.fileName, 63 );
		file.readIdLong( "EncyclopediaID", bldg.encycloID );


		bool bIsTurret = 0;

		if ( NO_ERR != file.seekBlock( "BuildingData" ) )
		{
			if ( NO_ERR != file.seekBlock( "GateData" ) )
			{
				if ( NO_ERR != file.seekBlock( "TurretData" ) )
				{
					if ( NO_ERR != file.seekBlock( "General" ) ) // hack for artillery piece
					{
						char errorStr[256];
						sprintf( errorStr, "coudn't find appropriate block in file %s", bldg.fileName );
						Assert( 0, 0, errorStr  );
					}
				}
				else
					bIsTurret = true;
			}
		}
		unsigned long tmp;
		file.readIdLong( "BuildingName", bldg.nameID );
		file.readIdULong( "DmgLevel", tmp );
		bldg.weight = tmp;
		if ( bIsTurret )
		{
			char weaponNameStr[64];
			strcpy( weaponNameStr, "WeaponType" );
			for ( int i = 0; i < 4; i++ )
			{
				
				file.readIdLong( weaponNameStr, bldg.componentIDs[i] );
				sprintf( weaponNameStr, "WeaponType%ld", i+1 );
			}
			
		}
		else
		{
			for ( int i = 0; i < 4; i++ )
			{
				bldg.componentIDs[i] = 0;
			}
		}
		
		bldg.scale = scale;
		buildings.Append( bldg );

		
	}

	return 0;
}

//*************************************************************************************************
LogisticsComponent* LogisticsData::getComponent( int componentID )
{
	for ( COMPONENT_LIST::EIterator iter = components.Begin();
		!iter.IsDone(); iter++ )
	{
			if ( ((*iter).getID() & 0x000000ff) == (componentID & 0x000000ff) )
				return &(*iter);
	}

	return NULL;
}

//*************************************************************************************************
LogisticsData::Building*			LogisticsData::getBuilding( int nameID )
{
	for ( BUILDING_LIST::EIterator iter = buildings.Begin();
		!iter.IsDone(); iter++ )
		{
			if ( (*iter).nameID == nameID )
				return &(*iter);
		}

	return NULL;
}


//*************************************************************************************************
int					LogisticsData::getBuildings( Building** bdgs, int& count )
{
	int maxCount = count;
	count = 0;
	int retVal = 0;

	for ( BUILDING_LIST::EIterator iter = buildings.Begin();
		!iter.IsDone(); iter++ )
	{
		if ( count < maxCount )
		{
			bdgs[count] = &(*iter);
		}
		else
			retVal = NEED_BIGGER_ARRAY;

		count++;
	}


	return retVal;
	
}

const EString&	LogisticsData::getCampaignName() const
{ 
	return missionInfo->getCampaignName();
}



bool				LogisticsData::campaignOver() 
{ 
	return missionInfo->campaignOver();
}
const char*			LogisticsData::getCurrentBigVideo() const 
{ 
	return missionInfo->getCurrentBigVideo(); 
}
const char*			LogisticsData::getFinalVideo() const
{ 
	return missionInfo->getFinalVideo();
}

void				LogisticsData::addNewBonusPurchaseFile( const char* pFileName )
{
	missionInfo->addBonusPurchaseFile( pFileName );
}

bool				LogisticsData::skipLogistics()
{
	return missionInfo->skipLogistics();
}
bool				LogisticsData::skipPilotReview()
{
	return missionInfo->skipPilotReview();
}
bool				LogisticsData::skipSalvageScreen()
{
	return missionInfo->skipSalvageScreen();
}
bool				LogisticsData::skipPurchasing()
{
	return missionInfo->skipPurchasing();
}

bool				LogisticsData::showChooseMission()
{
	return missionInfo->showChooseMission();
}

void	LogisticsData::setSingleMission( const char* pName )
{
	missionInfo->setSingleMission( pName );
	clearVariants();
//	initPilots(); // reset pilotsb
	clearInventory();
	initPilots(); // reset pilotsb //magic 02062012 bugfix initPilots before clearInventory
	updateAvailability();
}

bool	LogisticsData::isSingleMission()
{
	if ( missionInfo )
	{
		return missionInfo->isSingleMission();
	}
	
	return 0;
}

bool LogisticsData::canHaveSalavageCraft()
{
	if ( !missionInfo )
		return true;
		
	return missionInfo->canHaveSalavageCraft();
}
bool LogisticsData::canHaveRepairTruck()
{
	if ( !missionInfo )
		return true;
	return missionInfo->canHaveRepairTruck();
}
bool LogisticsData::canHaveScoutCopter()
{
	if ( !missionInfo )
		return true;
	return missionInfo->canHaveScoutCopter();
}
bool LogisticsData::canHaveArtilleryPiece()
{
	if ( !missionInfo )
		return true;
	return missionInfo->canHaveArtilleryPiece();
}
bool LogisticsData::canHaveAirStrike()
{
	if ( !missionInfo )
		return true;
	return missionInfo->canHaveAirStrike();
}
bool LogisticsData::canHaveSensorStrike()
{
	if ( !missionInfo )
		return true;
	return missionInfo->canHaveSensorStrike();
}
bool LogisticsData::canHaveMineLayer()
{
	if ( !missionInfo )
		return true;
	return missionInfo->canHaveMineLayer();
}

bool				LogisticsData::getVideoShown()
{
	if ( !missionInfo )
		return true;
	return missionInfo->getVideoShown();
}
void				LogisticsData::setVideoShown()
{
	if ( missionInfo )
		missionInfo->setVideoShown();

}
void	LogisticsData::setPilotUnused( const char* pName )
{
	for ( PILOT_LIST::EIterator iter = pilots.Begin(); !iter.IsDone(); iter++  )
	{
		if ( (*iter).getFileName().Compare( pName, 0 ) == 0 )
		{
			(*iter).setUsed( 0 );
			break;
		}
	}


}

void LogisticsData::setCurrentMissionNum (long cMission)
{
	missionInfo->setCurrentMissionNumber(cMission);
}

long LogisticsData::getCurrentMissionNum (void)
{
	return missionInfo->getCurrentMissionNumber();
}
void LogisticsData::getComponentsInventory( EList<LogisticsComponent*, LogisticsComponent*>& newList )
{
	for ( ICOMPONENT_LIST::EIterator iter = icomponents.Begin(); !iter.IsDone(); iter++ )
	{

		newList.Append( (*iter) );
	}
}

long LogisticsData::loadComponent( FitIniFile& file, int& count )
{
	//char tmp[256];
	//Magic begin
	//for ( int i = 0; i < componentCount; i++ )
	//{
	//	sprintf( tmp, "Component%ld", i );
	//	file.readIdLong(tmp, id );
	//}
	//Magic end
return 0;
}
void LogisticsData::clearComponentsInventory()
{
	//M10 erased
	//for (ICOMPONENT_LIST::EIterator iter = icomponents.Begin(); !iter.IsDone(); iter++ )
	//{
	//	delete *iter;
	//}

	icomponents.Clear();
}

void LogisticsData::removeComponentFromInventory( int weaponID )
{
	for ( ICOMPONENT_LIST::EIterator iter = icomponents.Begin(); !iter.IsDone(); iter++ )
	{
		if ( ((*iter)->getID() & 0x000000ff) == (weaponID & 0x000000ff) )
		{
			LogisticsComponent* inComp = getComponent( weaponID );
			icomponents.Delete( icomponents.Find( inComp ) );
		}
	}

}
void LogisticsData::removeComponentFromInventory( LogisticsComponent* mComp )
{
		if ( !mComp )
	{
		gosASSERT(!"MAGIC error if removeComponentFromInventory" );
		
	}
	//for ( ICOMPONENT_LIST::EIterator iter = icomponents.Begin(); !iter.IsDone(); iter++ )
	//{
	//	if ( ((*iter)->getID() & 0x000000ff) == (weaponID & 0x000000ff) )
	//	{
			//LogisticsComponent* inComp = getComponent( weaponID );
			icomponents.Delete( icomponents.Find( mComp ) );
	//	}
	//}

}
int	LogisticsData::getAllInventoryComponents( LogisticsComponent** pComps, int& maxCount )
{
	int retVal = 0;
	
	int i = 0;
	for ( ICOMPONENT_LIST::EIterator iter = icomponents.Begin(); 
		!iter.IsDone(); iter++ )
		{
			if ( i < maxCount )
					pComps[i]	= (*iter);//&(*iter);	
				else 
					retVal = NEED_BIGGER_ARRAY;
				++i;
		}

	maxCount = icomponents.Count();

	return retVal; 
}

int LogisticsData::addComponentToInventory( LogisticsComponent* iComp )
{
	if ( !iComp )
	{
		gosASSERT(!"MAGIC error i addComponentToInventory" );
		return -1;
	}
	//int RP = pVariant->getCost();

	//if ( missionInfo->getCBills() - RP >= 0 )
	//{
//		int count = instance->createInstanceID( iComp );
//		LogisticsComponent* pM = new LogisticsComponent( iComp );
		icomponents.Append( iComp );
		//missionInfo->decrementCBills( pVariant->getCost() );
		//Magic begin
			//remove mech from purchasing list
		//Meagic end
		return 0;
	//}

	//return NOT_ENOUGH_RESOURCE_POINTS;
}
void LogisticsData::UpdateCinventory(LogisticsComponent** pComp, int& maxCount)
{
		icomponents.Clear(); 
			for ( int i = 0; i < maxCount; i++ )
			{
				icomponents.Append( pComp[i] );	
			}			
		maxCount = icomponents.Count();
}

void LogisticsData::UpdatePinventory(LogisticsComponent** pComp, int& maxCount)
{
		purcomponents.Clear(); 
			for ( int i = 0; i < maxCount; i++ )
			{
				purcomponents.Append( pComp[i] );	
			}			
		maxCount = purcomponents.Count();
}

long LogisticsData::LoadMechsForPurchase(LogisticsVariant** PurVar, int& PurVarCount)
{
	PurVarCount = 0;

	char tmp[32];
	EString purchaseFileName = missionInfo->getCurrentPurchaseFile();
	purchaseFileName.MakeLower();

	if ( purchaseFileName.Length() < 1 )
	{
		purchaseFileName = missionPath;
		purchaseFileName += "purchase.fit";
	}

	// make sure its around and you can open it 
	FitIniFile file;
	if ( NO_ERR != file.open( (char*)(const char*)purchaseFileName ) )
	{
		EString error;
		error.Format( "Couldn't open %s", (char*)(const char*)purchaseFileName );
		PAUSE(((char*)(const char*)error ));
		return NO_PURCHASE_FILE;
	}

	// go through each variant, and see if it's available
	char chassisFileName[255];

	int varNum;

	varNum = RandomNumber(5);
	//varNum = 0;
	
	file.seekBlock( "Mechs" );
	int skipMe = 0;
	for ( int i = 0; i < 255; i++ )
	{
		sprintf( tmp, "Mech%ld", i );
		if ( NO_ERR != file.readIdString( tmp, chassisFileName, 254 ) )
			break;
		//M11 begin
		FullPathFileName path;
		path.init( objectPath, chassisFileName, ".csv" );

		CSVFile mechCSVFile;
			if ( mechCSVFile.open(path) == NO_ERR )
			{
				PurVar[i - skipMe] = getPurVariant( chassisFileName, varNum );
				PurVarCount++;
				mechCSVFile.close();
			}
			else skipMe++;
		///M11 end
	}

	file.close();
	return 0;

}

EString LogisticsData::getPurchaseFileName()
{
	return (missionInfo->getCurrentPurchaseFile());
}

LogisticsVariant* LogisticsData::getPurVariant( const char* pCSVFileName, int VariantNum )
{
	bool found = false;
	EString lowerCase = pCSVFileName;
	lowerCase.MakeLower();
	for( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
	{
		if ( -1 !=( (*iter)->getFileName().Find( lowerCase, -1 ) )
			&& (((*iter)->getVariantID()>>16)&0xff) == VariantNum )
		{
			return *iter;
			found = true;
		}
	}
	if (!found)
	{
		VariantNum = 0;
		for( VARIANT_LIST::EIterator iter = variants.Begin(); !iter.IsDone(); iter++ )
		{
			if ( -1 !=( (*iter)->getFileName().Find( lowerCase, -1 ) )
				&& (((*iter)->getVariantID()>>16)&0xff) == VariantNum )
			{
				return *iter;
				//found = true;
			}
		}
	}

	return NULL;
}

long LogisticsData::initPurVariants()
{
	int PurVarCount = 0;
	LogisticsVariant* PurVar[256];

	char tmp[32];
	EString purchaseFileName = missionInfo->getCurrentPurchaseFile();
	purchaseFileName.MakeLower();

	if ( purchaseFileName.Length() < 1 )
	{
		purchaseFileName = missionPath;
		purchaseFileName += "purchase.fit";
	}

	// make sure its around and you can open it 
	FitIniFile file;
	if ( NO_ERR != file.open( (char*)(const char*)purchaseFileName ) )
	{
		EString error;
		error.Format( "Couldn't open %s", (char*)(const char*)purchaseFileName );
		PAUSE(((char*)(const char*)error ));
		return NO_PURCHASE_FILE;
	}

	// go through each variant, and see if it's available
	char chassisFileName[255];

	int varNum;

	varNum = RandomNumber(5);
	
	/*//Magic 100 Debug file begin
	char tmp2[256];
	FitIniFile MtestFile;
	MtestFile.open("data\\purvartest.fit", CREATE);
	sprintf( tmp2, "random variant num %ld", varNum );
	MtestFile.writeBlock( tmp2 );
	// magic 100 end*/


	file.seekBlock( "Mechs" );
	//magic 101 begin
	int skipMe = 0;
	//magic 101 end
	for ( int i = 0; i < 255; i++ )
	{
		sprintf( tmp, "Mech%ld", i );
		if ( NO_ERR != file.readIdString( tmp, chassisFileName, 254 ) )
			break;
		/*//magic 100 begin
		sprintf( tmp2, "tmp %s",tmp );
		MtestFile.writeBlock( tmp2 );
		sprintf( tmp2, "chasis file name %s",chassisFileName );
		MtestFile.writeBlock( tmp2 );
		//magic 100 end*/
		FullPathFileName path;
		path.init( objectPath, chassisFileName, ".csv" );

		CSVFile mechCSVFile;

			if (mechCSVFile.open(path) == NO_ERR )
			{
				PurVar[i - skipMe] = getPurVariant( chassisFileName, varNum );
				PurVarCount++;
				mechCSVFile.close();
				//sprintf( tmp2, "chasis file name exist %s",chassisFileName );
				//MtestFile.writeBlock( tmp2 );

			}
			else	skipMe++;

	}
	UpdatePurVariants( PurVar, PurVarCount);
	/*//magic 100 begin
		sprintf( tmp2, "purchase variant count %ld", PurVarCount);
		MtestFile.writeBlock( tmp2 );
		MtestFile.close();
	//Magic 100 debug file end*/

//Magic begin load components for purchase


	file.seekBlock( "Components" );


	char tmp1[256];
	long component;
	long componentCount = 0;
	purcomponents.Clear();
	//FitIniFile MtestFile;
	//MtestFile.open("data\\mtest.fit", CREATE);
	//MtestFile.writeIdLong( tmp1, iComponentCount );
	for ( int i = 0; i < 255; i++ )
	{
		sprintf( tmp1, "Component%ld", i );
		//MtestFile.writeBlock( tmp1 );
		
		if ( NO_ERR != file.readIdLong( tmp1, component ) )
			break;

		for ( COMPONENT_LIST::EIterator cIter = components.Begin(); !cIter.IsDone(); cIter++ )
		{
			if ( (*cIter).getID() == component )
			{
				LogisticsComponent* inComp = new LogisticsComponent( (*cIter) );
				purcomponents.Append( inComp );
				componentCount++;
			}
		}
	}

//Magic end


	file.close();
	return 0;

}

void LogisticsData::UpdatePurVariants(LogisticsVariant** pVar, int& maxCount)
{
		purvariants.Clear(); 
		for ( int i = 0; i < maxCount; i++ )
			{
				purvariants.Append( pVar[i] );	
			}			
		maxCount = purvariants.Count();
}
void	LogisticsData::getPurVariants( EList<LogisticsVariant*, LogisticsVariant*>& newList )
{
	for ( VARIANT_LIST::EIterator iter = purvariants.Begin(); !iter.IsDone(); iter++ )
	{
		//Magic 68 begin
			if ( (*iter)->isDesignerMech() )
			{
				newList.Append( (*iter) );
			}
		//Magic 68 end
		//newList.Append( (*iter) ); //Magic 68 disabled
	}
}

int LogisticsData::incrementMaxDropWeight( int amount)
{
	missionInfo->decrementCBills( 10000 );
	return  missionInfo->incMaxDropWeight(amount); //M17

}

int LogisticsData::decrementMaxDropWeight( int amount)
{
	missionInfo->incrementCBills( 10000 );
	return  missionInfo->decMaxDropWeight(amount); //M17

}

long LogisticsData::loadPurMech( FitIniFile& file, int& count )
{
	char tmp[256];
	file.readIdString( "Variant", tmp, 255 );
	for ( VARIANT_LIST::EIterator mIter = variants.Begin(); !mIter.IsDone(); mIter++ )
	{
		if ( (*mIter)->getName().Compare( tmp, 0 ) == 0 )
		{
			
			//LogisticsMech* pMech = new LogisticsMech( (*mIter), count );
			//file.readIdString( "Pilot", tmp, 255 );
			purvariants.Append( (*mIter) );
			return 0;
		}
	}

	return -1; // failed in finding the variant

}
int	LogisticsData::getAllPurchaseComponents( LogisticsComponent** pComps, int& maxCount )
{
	int retVal = 0;
	
	int i = 0;
	for ( ICOMPONENT_LIST::EIterator iter = purcomponents.Begin(); 
		!iter.IsDone(); iter++ )
		{
			if ( i < maxCount )
					pComps[i]	= (*iter);//&(*iter);	
				else 
					retVal = NEED_BIGGER_ARRAY;
				++i;
		}

	maxCount = purcomponents.Count();

	return retVal; 
}

bool LogisticsData::toggleRepairTruck() //NEW magic 15072011
{
	if ( !missionInfo )
		return true;
	return missionInfo->toggleRepairTruck();
}

bool LogisticsData::toggleSalvageCraft() //NEW magic 29082011
{
	if ( !missionInfo )
		return true;
	return missionInfo->toggleSalvageCraft();
}
//magic 25052012 begin
bool LogisticsData::toggleSensorStrike() //NEW
{
	if ( !missionInfo )
		return true;
	return missionInfo->toggleSensorStrike();
}

bool LogisticsData::toggleArtilleryPiece() //NEW
{
	if ( !missionInfo )
		return true;
	return missionInfo->toggleArtilleryPiece();
}

bool LogisticsData::toggleAirStrike() //NEW
{
	if ( !missionInfo )
		return true;
	return missionInfo->toggleAirStrike();
}
//magic 25052012 end
//magic 02062012 begin
int LogisticsData::addRP( int amount)
{
	missionInfo->decrementCBills( amount );
	return  missionInfo->addRP(amount);
	//addResourcePoints( amount ); //magic 03062012 no effect
}

int LogisticsData::setRP( int amount)
{
	return  missionInfo->setRP(amount);
}

int LogisticsData::setMaxDropWeight(int amount)
{
	return  missionInfo->setMaxDropWeight(amount);
}
//magic 02062012 end
//magic 03092012 begin
void	LogisticsData::addNewComponentSalvage( const char* pFileName )
{

	FitIniFile file;
	if ( NO_ERR != file.open( pFileName ) )
	{
		Assert( 0, 0, "coudln't find the salvage file\n" );
	}

	file.seekBlock( "General" );	
	
	long ComponentCount;
	ComponentCount = 0;
	
	file.readIdLong( "ComponentCount", ComponentCount );//Magic

	//load components inventory
	long result1 = file.seekBlock( "Components" );
	if ( result1 != NO_ERR )
	{
		Assert( 0, 0, "No components in the salvage file" );
	}

	char tmp1[256];
	long component;
	//icomponents.Clear();
	//FitIniFile MtestFile;
	//MtestFile.open("data\\mtest.fit", CREATE);
	//MtestFile.writeIdLong( tmp1, iComponentCount );
	for ( int i = 0; i < ComponentCount; i++ )
	{
		sprintf( tmp1, "Component%ld", i );
		//MtestFile.writeBlock( tmp1 );
		
		if ( NO_ERR != file.readIdLong( tmp1, component ) )
			break;

		for ( COMPONENT_LIST::EIterator cIter = components.Begin(); !cIter.IsDone(); cIter++ )
		{
			if ( (*cIter).getID() == component )
			{
				LogisticsComponent* inComp = new LogisticsComponent( (*cIter) );
				//icomponents.Append( inComp ); //magic 04092012 disabled -OK but dont want to append to inventory before mission success
				salvageComponents.Append( inComp ); //magic 04092012
			}
		}
	}

	file.close();

}
//magic 03092012 end
//magic 04092012 begin
long	LogisticsData::quickSaveSalvageComponents( FitIniFile& file )
{
	file.writeBlock( "SalvageComponents" );
	file.writeIdLong( "SalvageComponentCount", salvageComponents.Count() );

	int IcomponentCount = 0;
	// save components salvage
	for ( ICOMPONENT_LIST::EIterator cIter = salvageComponents.Begin();	!cIter.IsDone(); cIter++ )
		{
			char tmp[32];
			sprintf( tmp, "SalvageComponent%ld", IcomponentCount );
			file.writeIdLong( tmp, (*cIter)->getID() );
			IcomponentCount++;
			//(*cIter)->save( file, IcomponentCount++ );
		}
	return 0;
}

long	LogisticsData::quickLoadSalvageComponents( FitIniFile& file )
{
	salvageComponents.Clear();
	//file.seekBlock( "SalvageComponents" );	
	long result1 = file.seekBlock( "SalvageComponents" );
	if ( result1 != NO_ERR )
	{
		Assert( 0, 0, "No components in the quickload salvage file" );
	}

	long ComponentCount = 0;
	
	file.readIdLong( "SalvageComponentCount", ComponentCount );

	char tmp1[256];
	long component;

	for ( int i = 0; i < ComponentCount; i++ )
	{
		sprintf( tmp1, "SalvageComponent%ld", i );
		
		if ( NO_ERR != file.readIdLong( tmp1, component ) )
			break;

		for ( COMPONENT_LIST::EIterator cIter = components.Begin(); !cIter.IsDone(); cIter++ )
		{
			if ( (*cIter).getID() == component )
			{
				LogisticsComponent* inComp = new LogisticsComponent( (*cIter) );
				salvageComponents.Append( inComp ); //magic 04092012
			}
		}
	}
	return 0;
}
//magic 04092012 end
//*************************************************************************************************
// end of file ( LogisticsData.cpp )
