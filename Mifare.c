#include "Mifare.h"
#include "errors.h"

#include <stdio.h>


#define TX_RX_BUFFER_SIZE           128 // 128 Byte buffer
#define DATA_BUFFER_LEN             16  /* Buffer length */
#define MFC_BLOCK_DATA_SIZE         4   /* Block Data size - 16 Bytes */
#define PHAL_MFC_VERSION_LENGTH     0x08 // from src/phalMFC_Int.h

/*******************************************************************************
**   Global Variable Declaration
*******************************************************************************/
phbalReg_Stub_DataParams_t sBalReader;  /* BAL component holder */

    /*
     * HAL variables
     */
phhalHw_Nfc_Ic_DataParams_t sHal_Nfc_Ic;        /* HAL component holder for Nfc Ic's */
void *pHal;                     /* HAL pointer */
uint8_t bHalBufferTx[TX_RX_BUFFER_SIZE];        /* HAL TX buffer */
uint8_t bHalBufferRx[TX_RX_BUFFER_SIZE];        /* HAL RX buffer */

    /*
     * PAL variables
     */
phpalI14443p3a_Sw_DataParams_t spalI14443p3a;   /* PAL I14443-A component */
phpalI14443p4a_Sw_DataParams_t spalI14443p4a;   /* PAL ISO I14443-4A component */
phpalI14443p3b_Sw_DataParams_t spalI14443p3b;   /* PAL ISO I14443-B component */
phpalI14443p4_Sw_DataParams_t spalI14443p4;     /* PAL ISO I14443-4 component */
phpalMifare_Sw_DataParams_t spalMifare; /* PAL MIFARE component */

phacDiscLoop_Sw_DataParams_t sDiscLoop; /* Discovery loop component */
phalMfc_Sw_DataParams_t salMfc; /* MIFARE Classic parameter structure */

uint8_t bDataBuffer[DATA_BUFFER_LEN];   /* universal data buffer */

uint8_t bSak;                   /* SAK card type information */
uint16_t wAtqa;                 /* ATQA card type information */
uint8_t blockcnt = 0;

/* Empty data */
uint8_t CLEAR_DATA[PHAL_MFUL_WRITE_BLOCK_LENGTH];

/** General information bytes to be sent with ATR */
const uint8_t GI[] = { 0x46, 0x66, 0x6D,
    0x01, 0x01, 0x10, /*VERSION*/ 0x03, 0x02, 0x00, 0x01, /*WKS*/ 0x04, 0x01, 0xF1 /*LTO*/
};

static uint8_t aData[50];       /* ATR response holder */

static phStatus_t LoadProfile(void)
{
    phStatus_t status = PH_ERR_SUCCESS;

    sDiscLoop.pPal1443p3aDataParams = &spalI14443p3a;
    sDiscLoop.pPal1443p3bDataParams = &spalI14443p3b;
    sDiscLoop.pPal1443p4aDataParams = &spalI14443p4a;
    sDiscLoop.pPal14443p4DataParams = &spalI14443p4;
    sDiscLoop.pHalDataParams = &sHal_Nfc_Ic.sHal;

    /*
     * These lines are added just to SIGSEG fault when non 14443-3 card is detected
     */
    /*
     * Assign the GI for Type A
     */
    sDiscLoop.sTypeATargetInfo.sTypeA_P2P.pGi = (uint8_t *) GI;
    sDiscLoop.sTypeATargetInfo.sTypeA_P2P.bGiLength = sizeof(GI);
    /*
     * Assign the GI for Type F
     */
    sDiscLoop.sTypeFTargetInfo.sTypeF_P2P.pGi = (uint8_t *) GI;
    sDiscLoop.sTypeFTargetInfo.sTypeF_P2P.bGiLength = sizeof(GI);
    /*
     * Assign ATR response for Type A
     */
    sDiscLoop.sTypeATargetInfo.sTypeA_P2P.pAtrRes = aData;
    /*
     * Assign ATR response for Type F
     */
    sDiscLoop.sTypeFTargetInfo.sTypeF_P2P.pAtrRes = aData;
    /*
     * Assign ATS buffer for Type A
     */
    sDiscLoop.sTypeATargetInfo.sTypeA_I3P4.pAts = aData;
    /*
     ******************************************************************************************** */

    /*
     * Passive Bailout bitmap configuration
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_BAIL_OUT, PH_OFF);
    PH_CHECK_SUCCESS(status);

    /*
     * Passive poll bitmap configuration. Poll for only Type A Tags.
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_PAS_POLL_TECH_CFG, PHAC_DISCLOOP_POS_BIT_MASK_A);
    PH_CHECK_SUCCESS(status);

    /*
     * Turn OFF Passive Listen.
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_PAS_LIS_TECH_CFG, PH_OFF);
    PH_CHECK_SUCCESS(status);

    /*
     * Turn OFF active listen.
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_ACT_LIS_TECH_CFG, PH_OFF);
    PH_CHECK_SUCCESS(status);

    /*
     * Turn OFF Active Poll
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_ACT_POLL_TECH_CFG, PH_OFF);
    PH_CHECK_SUCCESS(status);

    /*
     * Disable LPCD feature.
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_ENABLE_LPCD, PH_OFF);
    PH_CHECK_SUCCESS(status);

    /*
     * reset collision Pending
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_COLLISION_PENDING, PH_OFF);
    PH_CHECK_SUCCESS(status);

    /*
     * whether anti-collision is supported or not.
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_ANTI_COLL, PH_ON);
    PH_CHECK_SUCCESS(status);

    /*
     * Device limit for Type A
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_TYPEA_DEVICE_LIMIT, PH_ON);
    PH_CHECK_SUCCESS(status);

    /*
     * Discovery loop Operation mode
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_OPE_MODE, RD_LIB_MODE_NFC);
    PH_CHECK_SUCCESS(status);

    /*
     * Bailout on Type A detect
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_BAIL_OUT, PHAC_DISCLOOP_POS_BIT_MASK_A);
    PH_CHECK_SUCCESS(status);

    /*
     * Return Status
     */
    return status;
}

phStatus_t NfcRdLibInit(void)
{
    phStatus_t status;

    /*
     * Initialize the Reader BAL (Bus Abstraction Layer) component
     */
    status = phbalReg_Stub_Init(&sBalReader, sizeof(phbalReg_Stub_DataParams_t));
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the OSAL Events.
     */
    status = phOsal_Event_Init();
    PH_CHECK_SUCCESS(status);

    // Start interrupt thread
    Set_Interrupt();

    /*
     * Set HAL type in BAL
     */
#ifdef NXPBUILD__PHHAL_HW_PN5180
    status = phbalReg_SetConfig(&sBalReader, PHBAL_REG_CONFIG_HAL_HW_TYPE, PHBAL_REG_HAL_HW_PN5180);
#endif
#ifdef NXPBUILD__PHHAL_HW_RC523
    status = phbalReg_SetConfig(&sBalReader, PHBAL_REG_CONFIG_HAL_HW_TYPE, PHBAL_REG_HAL_HW_RC523);
#endif
#ifdef NXPBUILD__PHHAL_HW_RC663
    status = phbalReg_SetConfig(&sBalReader, PHBAL_REG_CONFIG_HAL_HW_TYPE, PHBAL_REG_HAL_HW_RC663);
#endif
    PH_CHECK_SUCCESS(status);

    status = phbalReg_SetPort(&sBalReader, (uint8_t *) SPI_CONFIG);
    PH_CHECK_SUCCESS(status);

    /*
     * Open BAL
     */
    status = phbalReg_OpenPort(&sBalReader);
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the Reader HAL (Hardware Abstraction Layer) component
     */
    status = phhalHw_Nfc_IC_Init(&sHal_Nfc_Ic,
                                 sizeof(phhalHw_Nfc_Ic_DataParams_t),
                                 &sBalReader,
                                 0, bHalBufferTx, sizeof(bHalBufferTx), bHalBufferRx, sizeof(bHalBufferRx));
    PH_CHECK_SUCCESS(status);

    /*
     * Set the parameter to use the SPI interface
     */
    sHal_Nfc_Ic.sHal.bBalConnectionType = PHHAL_HW_BAL_CONNECTION_SPI;

    Configure_Device(&sHal_Nfc_Ic);

    /*
     * Set the generic pointer
     */
    pHal = &sHal_Nfc_Ic.sHal;

    /*
     * Initializing specific objects for the communication with MIFARE (R) Classic cards. The MIFARE (R) Classic card
     * is compliant of ISO 14443-3 and ISO 14443-4
     */

    /*
     * Initialize the I14443-A PAL layer
     */
    status = phpalI14443p3a_Sw_Init(&spalI14443p3a, sizeof(phpalI14443p3a_Sw_DataParams_t), &sHal_Nfc_Ic.sHal);
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the I14443-A PAL component
     */
    status = phpalI14443p4a_Sw_Init(&spalI14443p4a, sizeof(phpalI14443p4a_Sw_DataParams_t), &sHal_Nfc_Ic.sHal);
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the I14443-4 PAL component
     */
    status = phpalI14443p4_Sw_Init(&spalI14443p4, sizeof(phpalI14443p4_Sw_DataParams_t), &sHal_Nfc_Ic.sHal);
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the I14443-B PAL component
     */
    status = phpalI14443p3b_Sw_Init(&spalI14443p3b, sizeof(phpalI14443p3b_Sw_DataParams_t), &sHal_Nfc_Ic.sHal);
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the MIFARE PAL component
     */
    status = phpalMifare_Sw_Init(&spalMifare, sizeof(phpalMifare_Sw_DataParams_t), &sHal_Nfc_Ic.sHal, NULL);
    PH_CHECK_SUCCESS(status);

    /*
     * Initialize the discover component
     */
    status = phacDiscLoop_Sw_Init(&sDiscLoop, sizeof(phacDiscLoop_Sw_DataParams_t), &sHal_Nfc_Ic.sHal);
    PH_CHECK_SUCCESS(status);

    /*
     * Load profile for Discovery loop
     */
    status = LoadProfile();
    PH_CHECK_SUCCESS(status);

    status = phalMfc_Sw_Init(&salMfc, sizeof(phalMfc_Sw_DataParams_t), &spalMifare, NULL);
    PH_CHECK_SUCCESS(status);

    /*
     * Read the version of the reader IC
     */
#if defined NXPBUILD__PHHAL_HW_RC523
    status = phhalHw_Rc523_ReadRegister(&sHal_Nfc_Ic.sHal, PHHAL_HW_RC523_REG_VERSION, &bDataBuffer[0]);
#endif
#if defined NXPBUILD__PHHAL_HW_RC663
    status = phhalHw_Rc663_ReadRegister(&sHal_Nfc_Ic.sHal, PHHAL_HW_RC663_REG_VERSION, &bDataBuffer[0]);
#endif
    PH_CHECK_SUCCESS(status);

    /*
     * Return Success
     */
    return PH_ERR_SUCCESS;
}

PyObject *Mifare_init(Mifare * self, PyObject * args, PyObject * kwds)
{
    int ret;
    ret = Set_Interface_Link();
    if (handle_error(ret, InitError)) return NULL;

    Reset_reader_device();

    ret = NfcRdLibInit();
    if (handle_error(ret, InitError)) return NULL;

    //prep clear data
    int i;
    for (i=0; i<PHAL_MFUL_WRITE_BLOCK_LENGTH; i++) {
        CLEAR_DATA[i] = 0;
    }

    Py_RETURN_NONE;
}

PyObject *Mifare_select(Mifare * self)
{
    phStatus_t status = 0;
    uint16_t wTagsDetected = 0;

    /*
     * Field OFF
     */
    status = phhalHw_FieldOff(pHal);
    CHECK_STATUS(status);
    if (handle_error(status, SelectError)) return NULL;
    
    /*
     * Configure Discovery loop for Poll Mode
     */
    status = phacDiscLoop_SetConfig(&sDiscLoop,
                                    PHAC_DISCLOOP_CONFIG_NEXT_POLL_STATE,
                                    PHAC_DISCLOOP_POLL_STATE_DETECTION);
    CHECK_STATUS(status);
    if (handle_error(status, SelectError)) return NULL;
    
    /*
     * Run Discovery loop
     */
    status = phacDiscLoop_Run(&sDiscLoop, PHAC_DISCLOOP_ENTRY_POINT_POLL);
    if ((status & PH_ERR_MASK) != PHAC_DISCLOOP_DEVICE_ACTIVATED) {
        if (handle_error(status, SelectError)) {
            return NULL;
        } else { // handle_error should catch everything, but if it doesn't
            return PyErr_Format(SelectError, "DiscLoop_Run command failed: %02X", (status & PH_ERR_MASK));
        }
    }
    
    /*
     * Card detected
     * Get the tag types detected info
     */
    status = phacDiscLoop_GetConfig(&sDiscLoop, PHAC_DISCLOOP_CONFIG_TECH_DETECTED, &wTagsDetected);
    if (handle_error(status, SelectError)) return NULL;
    /*
     * Check for Type A tag detection
     */
    if (PHAC_DISCLOOP_CHECK_ANDMASK(wTagsDetected, PHAC_DISCLOOP_POS_BIT_MASK_A)) {

        uint8_t byteBufferSize = sDiscLoop.sTypeATargetInfo.aTypeA_I3P3[0].bUidSize + 1;
        uint8_t i;
        char asciiBuffer[sDiscLoop.sTypeATargetInfo.aTypeA_I3P3[0].bUidSize + 1];
        if (byteBufferSize + 1 > sDiscLoop.sTypeATargetInfo.aTypeA_I3P3[0].bUidSize + 1) {
            // Truncate if we got back too much data
            byteBufferSize = sDiscLoop.sTypeATargetInfo.aTypeA_I3P3[0].bUidSize;
        }

        for (i = 0; i < byteBufferSize; i++) {
            sprintf(&asciiBuffer[2 * i], "%02X", sDiscLoop.sTypeATargetInfo.aTypeA_I3P3[0].aUid[i]);
        }

        return PyUnicode_FromString(asciiBuffer);

    } else {
        return PyErr_Format(SelectError, "DISCLOOP_CHECK_ANDMASK failed: %02X", (status & PH_ERR_MASK));
    }
    Py_RETURN_NONE;
}
PyObject *Mifare_get_type(Mifare * self)
{
	uint8_t t= sDiscLoop.bDetectedTechs;
	if(t==0x4)
	{
		return PyUnicode_FromString("MIFARE DESFire");
	}
	else if(t==0x3)
	{
		return PyUnicode_FromString("MIFARE Ultralight");
	}
	else if(t==0x2)
	{
		return PyUnicode_FromString("MIFARE Mini");
	}
	else if(t==0x0)
	{
		return PyUnicode_FromString("MIFARE Classic 1k");
	}
	else if(t==0x1)
	{
		return PyUnicode_FromString("MIFARE Classic 4k");
	}
	else
		Py_RETURN_NONE;
}
PyObject *Mifare_read_block(Mifare *self, PyObject *args)
{
    const size_t bufferSize = PHAL_MFUL_READ_BLOCK_LENGTH;
    uint8_t data[bufferSize];
    uint8_t blockIdx;
    phStatus_t status;

    if (!PyArg_ParseTuple(args, "b", &blockIdx)) {
		Py_RETURN_NONE;
    }
    status = phalMful_Read(&salMfc, blockIdx, bDataBuffer);
    if (handle_error(status, ReadError)) return NULL;

#if PY_MAJOR_VERSION >= 3
    return Py_BuildValue("y#", &bDataBuffer[0], MFC_BLOCK_DATA_SIZE);
#else
    return Py_BuildValue("s#", &bDataBuffer[0], MFC_BLOCK_DATA_SIZE);

#endif
}

PyObject *Mifare_read_sign(Mifare * self)
{
    const size_t bufferSize = PHAL_MFUL_SIG_LENGTH;
    uint8_t data[bufferSize];
    uint8_t *sign = data;

    phStatus_t status = 0;

    status = phalMful_ReadSign(&salMfc, '\0', &sign);
    if (handle_error(status, ReadError)) return NULL;

#if PY_MAJOR_VERSION >= 3
    return Py_BuildValue("y#", sign, bufferSize);
#else
    return Py_BuildValue("s#", sign, bufferSize);
#endif
}

PyObject *Mifare_write_block(Mifare * self, PyObject * args)
{
    phStatus_t status = 0;
    uint8_t blockIdx;
    uint8_t *data;
    int dataLen;

    if (!PyArg_ParseTuple(args, "bs#", &blockIdx, &data, &dataLen)) {
        return NULL;
    }

    if (dataLen != PHAL_MFUL_WRITE_BLOCK_LENGTH) {
        return PyErr_Format(WriteError, "Write data MUST be specified as %d bytes", PHAL_MFUL_WRITE_BLOCK_LENGTH);
    }

    status = phalMful_Write(&salMfc, blockIdx, data);
    if (handle_error(status, WriteError)) return NULL;

    Py_RETURN_NONE;
}

PyObject *Mifare_get_version(Mifare* self)
{
    const size_t bufferSize = PHAL_MFC_VERSION_LENGTH;
    unsigned char version[bufferSize];
    
    phStatus_t status = 0;
    
    status = phalMful_GetVersion(&salMfc, version);
    if (handle_error(status, ReadError)) return NULL;
    
    return Py_BuildValue("{s:B, s:B, s:B, s:B, s:B, s:B, s:B}",
                         "vendor\0",       version[1],
                         "tag_type\0",     version[2],
                         "tag_subtype\0",  version[3],
                         "version_major\0",version[4],
                         "version_minor\0",version[5],
                         "tag_size\0",     version[6],
                         "protocol\0",     version[7]
                        );
}

PyObject* Mifare_clear_block(Mifare* self, PyObject* args) {
    phStatus_t status = 0;
    uint8_t blockIdx;

    if (!PyArg_ParseTuple(args, "b", &blockIdx)) {
        return NULL;
    }

    status = phalMful_Write(&salMfc, blockIdx, CLEAR_DATA);
    if (handle_error(status, WriteError)) return NULL;

    Py_RETURN_NONE;
}

/***********************************
** Python Type Definiton
***********************************/
PyMethodDef Mifare_methods[] = {
    {"select", (PyCFunction) Mifare_select, METH_NOARGS, "Select a Mifare card if present. Returns the card UID"}
    ,
    {"read_block", (PyCFunction) Mifare_read_block, METH_VARARGS, "Read 4 bytes starting at the specified block."}
    ,
    {"read_sign", (PyCFunction) Mifare_read_sign, METH_NOARGS, "Read 32 bytes card manufacturer signature."}
    ,
    {"write_block", (PyCFunction) Mifare_write_block, METH_VARARGS, "Write 4 bytes starting at the specified block."}
    ,
    {"get_version", (PyCFunction) Mifare_get_version, METH_NOARGS, "Read version data as a dict."}
    ,
    {"clear_block", (PyCFunction) Mifare_clear_block, METH_VARARGS, "Clear 4 bytes starting at the specifed block."}
    ,
    {"get_type", (PyCFunction) Mifare_get_type, METH_VARARGS, "Return the type os the mifare card"}
    ,
    {"classic_authenticate",(PyCFunction) Mifare_get_type, METH_VARARGS, "Return the type os the mifare card"}
    ,
    {NULL}                      /* Sentinel */
};

PyObject *MifareClassic_Authenticate(Mifare *self, PyObject *args)
{
	uint8_t blockIdx;
    char *key;
    uint8_t keyLength=6;
    int i;
    PyObject* transferTuple;
    uint8_t KeyType;
    #if PY_MAJOR_VERSION >= 3
    if (!PyArg_ParseTuple(args, "bOb", &blockIdx, &transferTuple,&KeyType)) {
#else
    if (!PyArg_ParseTuple(args, "bOb", &blockIdx, &transferTuple,&KeyType)) {
#endif
		printf("Cry");
        return NULL;
    }
    key= malloc(sizeof(char)*6);
    for (i = 0; i < PyList_Size(transferTuple); i++) {
		uint8_t k;
		PyArg_Parse(PyList_GetItem(transferTuple, (Py_ssize_t)i),"b",&k);
		key[i]=k;
	}
}




PyTypeObject MifareType = {
    PyVarObject_HEAD_INIT(NULL, 0)
        "nxppy.Mifare",         /* tp_name */
    sizeof(Mifare),             /* tp_basicsize */
    0,                          /* tp_itemsize */
    0,                          /* tp_dealloc */
    0,                          /* tp_print */
    0,                          /* tp_getattr */
    0,                          /* tp_setattr */
    0,                          /* tp_reserved */
    0,                          /* tp_repr */
    0,                          /* tp_as_number */
    0,                          /* tp_as_sequence */
    0,                          /* tp_as_mapping */
    0,                          /* tp_hash */
    0,                          /* tp_call */
    0,                          /* tp_str */
    0,                          /* tp_getattro */
    0,                          /* tp_setattro */
    0,                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,         /* tp_flags */
    "Mifare objects",           /* tp_doc */
    0,                          /* tp_traverse */
    0,                          /* tp_clear */
    0,                          /* tp_richcompare */
    0,                          /* tp_weaklistoffset */
    0,                          /* tp_iter */
    0,                          /* tp_iternext */
    Mifare_methods,             /* tp_methods */
    0,                          /* tp_members */
    0,                          /* tp_getset */
    0,                          /* tp_base */
    0,                          /* tp_dict */
    0,                          /* tp_descr_get */
    0,                          /* tp_descr_set */
    0,                          /* tp_dictoffset */
    (initproc) Mifare_init,     /* tp_init */
};

