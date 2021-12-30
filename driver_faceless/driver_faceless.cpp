#include <openvr_driver.h>
#include "driverlog.h"

#include <vector>
#include <thread>
#include <chrono>

#if defined( _WINDOWS )
#include <windows.h>
#endif

using namespace vr;
using namespace std::chrono;

#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" __declspec( dllexport )
#define HMD_DLL_IMPORT extern "C" __declspec( dllimport )
#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)
#define HMD_DLL_EXPORT extern "C" __attribute__((visibility("default")))
#define HMD_DLL_IMPORT extern "C" 
#else
#error "Unsupported Platform."
#endif

// --- Math helpers ---
inline HmdQuaternion_t HmdQuaternion_Init( double w, double x, double y, double z )
{
	HmdQuaternion_t quat;
	quat.w = w;
	quat.x = x;
	quat.y = y;
	quat.z = z;
	return quat;
}

inline HmdQuaternion_t HmdQuaternion_FromEuler(double roll, double pitch, double yaw)
{
	HmdQuaternion_t quat;
	quat.x = sin(roll / 2) * cos(pitch / 2) * cos(yaw / 2) - cos(roll / 2) * sin(pitch / 2) * sin(yaw / 2);
	quat.y = cos(roll / 2) * sin(pitch / 2) * cos(yaw / 2) + sin(roll / 2) * cos(pitch / 2) * sin(yaw / 2);
	quat.z = cos(roll / 2) * cos(pitch / 2) * sin(yaw / 2) - sin(roll / 2) * sin(pitch / 2) * cos(yaw / 2);
	quat.w = cos(roll / 2) * cos(pitch / 2) * cos(yaw / 2) + sin(roll / 2) * sin(pitch / 2) * sin(yaw / 2);
	return quat;
}

inline void HmdMatrix34_Translation(HmdMatrix34_t mat, double translation[3])
{
	translation[0] = mat.m[0][3];
	translation[1] = mat.m[1][3];
	translation[2] = mat.m[2][3];
}

inline void HmdMatrix34_Rotation(HmdMatrix34_t mat, double euler[3])
{
	euler[0] = atan2(mat.m[2][1], mat.m[2][2]);
	euler[1] = atan2(-mat.m[2][0], sqrt(mat.m[2][1] * mat.m[2][1] + mat.m[2][2] * mat.m[2][2]));
	euler[2] = atan2(mat.m[1][0], mat.m[0][0]);
}

inline HmdQuaternion_t HmdMatrix34_FromQuat(const HmdMatrix34_t* matrix44m) {
	const float* matrix44 = matrix44m->m[0];
	float q[4];
	// Algorithm from http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
	float tr = matrix44[0] + matrix44[5] + matrix44[10];

	if (tr > 0) {
		float S = sqrtf(tr + 1.0f) * 2.f; // S=4*qw
		q[0] = 0.25f * S;
		q[1] = (matrix44[9] - matrix44[6]) / S;
		q[2] = (matrix44[2] - matrix44[8]) / S;
		q[3] = (matrix44[4] - matrix44[1]) / S;
	}
	else if ((matrix44[0] > matrix44[5]) && (matrix44[0] > matrix44[10])) {
		float S = sqrtf(1.0f + matrix44[0] - matrix44[5] - matrix44[10]) * 2.f; // S=4*qx
		q[0] = (matrix44[9] - matrix44[6]) / S;
		q[1] = 0.25f * S;
		q[2] = (matrix44[1] + matrix44[4]) / S;
		q[3] = (matrix44[2] + matrix44[8]) / S;
	}
	else if (matrix44[5] > matrix44[10]) {
		float S = sqrtf(1.0f + matrix44[5] - matrix44[0] - matrix44[10]) * 2.f; // S=4*qy
		q[0] = (matrix44[2] - matrix44[8]) / S;
		q[1] = (matrix44[1] + matrix44[4]) / S;
		q[2] = 0.25f * S;
		q[3] = (matrix44[6] + matrix44[9]) / S;
	}
	else {
		float S = sqrtf(1.0f + matrix44[10] - matrix44[0] - matrix44[5]) * 2.f; // S=4*qz
		q[0] = (matrix44[4] - matrix44[1]) / S;
		q[1] = (matrix44[2] + matrix44[8]) / S;
		q[2] = (matrix44[6] + matrix44[9]) / S;
		q[3] = 0.25f * S;
	}
	HmdQuaternion_t qr;
	qr.w = q[0];
	qr.x = q[1];
	qr.y = q[2];
	qr.z = q[3];
	return qr;
}

inline void HmdMatrix_SetIdentity( HmdMatrix34_t *pMatrix )
{
	pMatrix->m[0][0] = 1.f;
	pMatrix->m[0][1] = 0.f;
	pMatrix->m[0][2] = 0.f;
	pMatrix->m[0][3] = 0.f;
	pMatrix->m[1][0] = 0.f;
	pMatrix->m[1][1] = 1.f;
	pMatrix->m[1][2] = 0.f;
	pMatrix->m[1][3] = 0.f;
	pMatrix->m[2][0] = 0.f;
	pMatrix->m[2][1] = 0.f;
	pMatrix->m[2][2] = 1.f;
	pMatrix->m[2][3] = 0.f;
}

// --- JSON keys ---
static const char * const k_pch_Faceless_Section = "driver_faceless";
static const char * const k_pch_Faceless_SerialNumber_String = "serialNumber";
static const char * const k_pch_Faceless_ModelNumber_String = "modelNumber";
static const char * const k_pch_Faceless_RenderWidth_Int32 = "renderWidth";
static const char * const k_pch_Faceless_RenderHeight_Int32 = "renderHeight";
static const char * const k_pch_Faceless_DisplayFrequency_Float = "displayFrequency";

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
class CFacelessDeviceDriver : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent, vr::IVRVirtualDisplay
{
public:
	CFacelessDeviceDriver()
	{
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
		m_ulPropertyContainer = vr::k_ulInvalidPropertyContainer;

		DriverLog( "Using settings values\n" );
		m_flIPD = vr::VRSettings()->GetFloat(k_pch_SteamVR_Section, k_pch_SteamVR_IPD_Float);

		char buf[1024];
		vr::VRSettings()->GetString( k_pch_Faceless_Section, k_pch_Faceless_SerialNumber_String, buf, sizeof( buf ) );
		m_sSerialNumber = buf;

		vr::VRSettings()->GetString( k_pch_Faceless_Section, k_pch_Faceless_ModelNumber_String, buf, sizeof( buf ) );
		m_sModelNumber = buf;

		m_nRenderWidth = vr::VRSettings()->GetInt32(k_pch_Faceless_Section, k_pch_Faceless_RenderWidth_Int32);
		m_nRenderHeight = vr::VRSettings()->GetInt32(k_pch_Faceless_Section, k_pch_Faceless_RenderHeight_Int32);
		m_flDisplayFrequency = vr::VRSettings()->GetFloat(k_pch_Faceless_Section, k_pch_Faceless_DisplayFrequency_Float);

		DriverLog( "driver_faceless: Serial Number: %s\n", m_sSerialNumber.c_str() );
		DriverLog( "driver_faceless: Model Number: %s\n", m_sModelNumber.c_str() );
		DriverLog( "driver_faceless: Render Target: %d %d\n", m_nRenderWidth, m_nRenderHeight );
		DriverLog( "driver_faceless: Display Frequency: %f\n", m_flDisplayFrequency );
		DriverLog( "driver_faceless: IPD: %f\n", m_flIPD );
	}

	virtual ~CFacelessDeviceDriver()
	{
	}

	virtual void Present(const PresentInfo_t* pPresentInfo, uint32_t unPresentInfoSize)
	{
	}

	virtual void WaitForPresent()
	{
	}

	virtual bool GetTimeSinceLastVsync(float* pfSecondsSinceLastVsync, uint64_t* pulFrameCounter)
	{
		return false;
	}

	virtual EVRInitError Activate( vr::TrackedDeviceIndex_t unObjectId ) 
	{
		m_unObjectId = unObjectId;
		m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer( m_unObjectId );

		vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, Prop_ModelNumber_String, m_sModelNumber.c_str() );
		vr::VRProperties()->SetStringProperty( m_ulPropertyContainer, Prop_RenderModelName_String, m_sModelNumber.c_str() );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_UserIpdMeters_Float, m_flIPD );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_UserHeadToEyeDepthMeters_Float, 0 );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_DisplayFrequency_Float, m_flDisplayFrequency );
		vr::VRProperties()->SetFloatProperty( m_ulPropertyContainer, Prop_SecondsFromVsyncToPhotons_Float, 0 );

		// return a constant that's not 0 (invalid) or 1 (reserved for Oculus)
		vr::VRProperties()->SetUint64Property( m_ulPropertyContainer, Prop_CurrentUniverseId_Uint64, 251235 );

		// avoid "not fullscreen" warnings from vrmonitor
		vr::VRProperties()->SetBoolProperty( m_ulPropertyContainer, Prop_IsOnDesktop_Bool, false );

		return VRInitError_None;
	}

	virtual void Deactivate() 
	{
		m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
	}

	virtual void EnterStandby()
	{
	}

	void *GetComponent( const char *pchComponentNameAndVersion )
	{
		if ( !_stricmp( pchComponentNameAndVersion, vr::IVRDisplayComponent_Version ) )
		{
			return (vr::IVRDisplayComponent*)this;
		}
		if (!_stricmp(pchComponentNameAndVersion, vr::IVRVirtualDisplay_Version))
		{
			return (vr::IVRVirtualDisplay*)this;
		}

		return NULL;
	}

	virtual void PowerOff() 
	{
	}

	virtual void DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize ) 
	{
		if( unResponseBufferSize >= 1 )
			pchResponseBuffer[0] = 0;
	}

	virtual void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) 
	{
		*pnX = 0;
		*pnY = 0;
		*pnWidth = m_nRenderWidth;
		*pnHeight = m_nRenderHeight;
	}

	virtual bool IsDisplayOnDesktop() 
	{
		return true;
	}

	virtual bool IsDisplayRealDisplay() 
	{
		return false;
	}

	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) 
	{
		*pnWidth = m_nRenderWidth;
		*pnHeight = m_nRenderHeight;
	}

	virtual void GetEyeOutputViewport( EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) 
	{
		*pnY = 0;
		*pnWidth = m_nRenderWidth / 2;
		*pnHeight = m_nRenderHeight;
	
		if ( eEye == Eye_Left )
		{
			*pnX = 0;
		}
		else
		{
			*pnX = m_nRenderWidth / 2;
		}
	}

	virtual void GetProjectionRaw( EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) 
	{
		*pfLeft = -1.0;
		*pfRight = 1.0;
		*pfTop = -1.0;
		*pfBottom = 1.0;	
	}

	virtual DistortionCoordinates_t ComputeDistortion( EVREye eEye, float fU, float fV ) 
	{
		DistortionCoordinates_t coordinates;
		coordinates.rfBlue[0] = fU;
		coordinates.rfBlue[1] = fV;
		coordinates.rfGreen[0] = fU;
		coordinates.rfGreen[1] = fV;
		coordinates.rfRed[0] = fU;
		coordinates.rfRed[1] = fV;
		return coordinates;
	}

	virtual DriverPose_t GetPose() 
	{
		DriverPose_t pose = { 0 };
		pose.poseIsValid = true;
		pose.result = TrackingResult_Running_OK;
		pose.deviceIsConnected = true;

		// Identity spaces
		pose.qWorldFromDriverRotation = HmdQuaternion_Init(1, 0, 0, 0);
		pose.qDriverFromHeadRotation = HmdQuaternion_Init(1, 0, 0, 0);
		
		// Find 2 controllers
		std::vector<uint32_t> idx;
		vr::CVRPropertyHelpers* props = vr::VRProperties();
		for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++)
		{
			vr::PropertyContainerHandle_t container = props->TrackedDeviceToPropertyContainer(i);
			vr::ETrackedPropertyError err;
			int32_t deviceClass = props->GetInt32Property(container, vr::ETrackedDeviceProperty::Prop_DeviceClass_Int32, &err);
			if (deviceClass == vr::ETrackedDeviceClass::TrackedDeviceClass_Controller)
			{
				idx.push_back(i);
			}
		}

		// Get controller poses
		TrackedDevicePose_t hmdPose[k_unMaxTrackedDeviceCount];
		VRServerDriverHost()->GetRawTrackedDevicePoses(0, hmdPose, k_unMaxTrackedDeviceCount);

		// If valid pose
		if (idx.size() >= 2)
		{
			HmdMatrix34_t mat1 = hmdPose[idx[0]].mDeviceToAbsoluteTracking;
			HmdMatrix34_t mat2 = hmdPose[idx[1]].mDeviceToAbsoluteTracking;
			
			// get average controller position
			double pos1[3], pos2[3];
			HmdMatrix34_Translation(mat1, pos1);
			HmdMatrix34_Translation(mat2, pos2);
			pose.vecPosition[0] = 0.5 * (pos1[0] + pos2[0]);
			pose.vecPosition[1] = 0.5 * (pos1[1] + pos2[1]) + 0.3;
			pose.vecPosition[2] = 0.5 * (pos1[2] + pos2[2]);

			// guess rotation
			double euler[3];
			//HmdMatrix34_Rotation(mat1, euler);
			//pose.qRotation = HmdQuaternion_FromEuler(euler[0], euler[1], euler[2]);
			pose.qRotation = HmdMatrix34_FromQuat(&mat1);
		}

		return pose;
	}
	
	void RunFrame()
	{
		if ( m_unObjectId != vr::k_unTrackedDeviceIndexInvalid )
		{
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated( m_unObjectId, GetPose(), sizeof( DriverPose_t ) );
		}
	}

	std::string GetSerialNumber() const { return m_sSerialNumber; }

private:
	vr::TrackedDeviceIndex_t m_unObjectId;
	vr::PropertyContainerHandle_t m_ulPropertyContainer;

	std::string m_sSerialNumber;
	std::string m_sModelNumber;

	int32_t m_nRenderWidth;
	int32_t m_nRenderHeight;
	float m_flDisplayFrequency;
	float m_flIPD;
};

// --- Driver provider ---
class CFacelessServerDriver: public IServerTrackedDeviceProvider
{
public:
	virtual EVRInitError Init( vr::IVRDriverContext *pDriverContext ) ;
	virtual void Cleanup() ;
	virtual const char * const *GetInterfaceVersions() { return vr::k_InterfaceVersions; }
	virtual void RunFrame() ;
	virtual bool ShouldBlockStandbyMode()  { return false; }
	virtual void EnterStandby()  {}
	virtual void LeaveStandby()  {}

private:
	CFacelessDeviceDriver *m_pNullHmdLatest = nullptr;
};

CFacelessServerDriver g_serverDriverNull;

EVRInitError CFacelessServerDriver::Init( vr::IVRDriverContext *pDriverContext )
{
	VR_INIT_SERVER_DRIVER_CONTEXT( pDriverContext );
	InitDriverLog( vr::VRDriverLog() );

	m_pNullHmdLatest = new CFacelessDeviceDriver();
	vr::VRServerDriverHost()->TrackedDeviceAdded( m_pNullHmdLatest->GetSerialNumber().c_str(), vr::TrackedDeviceClass_HMD, m_pNullHmdLatest );

	return VRInitError_None;
}

void CFacelessServerDriver::Cleanup() 
{
	CleanupDriverLog();
	delete m_pNullHmdLatest;
	m_pNullHmdLatest = NULL;
}

void CFacelessServerDriver::RunFrame()
{
	if ( m_pNullHmdLatest )
	{
		m_pNullHmdLatest->RunFrame();
	}

	vr::VREvent_t vrEvent;
	while (vr::VRServerDriverHost()->PollNextEvent(&vrEvent, sizeof(vrEvent)))
	{
		// do nothing, no events to care about
	}
}

// --- Entry point ---
HMD_DLL_EXPORT void *HmdDriverFactory( const char *pInterfaceName, int *pReturnCode )
{
	if( 0 == strcmp( IServerTrackedDeviceProvider_Version, pInterfaceName ) )
	{
		return &g_serverDriverNull;
	}

	if( pReturnCode )
		*pReturnCode = VRInitError_Init_InterfaceNotFound;

	return NULL;
}
