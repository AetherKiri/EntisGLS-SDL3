"""Make the selected legacy switches' existing fallback explicit.

The cases implement a subset of the enum in each context. A non-matching
value continues after the switch as it did in the original SDK code.
"""
from pathlib import Path

_SWITCH_ENDS = {
    'glscs_assembler.cpp': (
        ('\t\t\tbreak ;\n', '\t\t}\n\t\tif ( ibuf.nTotalBytes < ibuf.nCodeBytes + nRefDataBytes )\n'),
        ('\t\t\tbreak ;\n', '\t\t}\n\t\tif ( IsTemporaryRegister( regRuntime2 ) )\n'),
    ),
    'glscs_classinf.cpp': (
        ('\t\t\t}\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\treturn\tfalse ;\n'),
    ),
    'glscs_compiler.cpp': (
        ('\t\tbreak ;\n', '\t}\n\tif ( fNakedThisVar )\n'),
        ('\t\t\tbreak ;\n', '\t\t}\n\t\tint\tregLoaded = regDst ;\n'),
        ('\t\treturn\tESLErrorMsg( "ポインタでない型への \\\'*\\\' アドレス参照です" ) ;\n', '\t}\n\treturn\tESLErrorMsg( "不正な特殊演算子です。" ) ;\n'),
        ('\t\t\t\t\t\t( "Array オブジェクトへのポインタは評価できません" ) ;\n', '\t\t\t\t}\n\t\t\t}\n'),
        ('\t\treturn\terr ;\n', '\t}\n\treturn\tESLErrorMsg( "不正な特殊演算子です。" ) ;\n'),
        ('\t\t\tpIntDst->SetValue( (BYTE) nValue ) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse if ( typeDst.IsLoadedRegister() )\n'),
        ('\t\t\t\t\t\t( regNum, ECSSakura2Processor::regMaskLow8 ) ;\n\t\t\t\t}\n\t\t\t\tbreak ;\n', '\t\t\t}\n\t\t}\n\t}\n\treturn\teslErrSuccess ;\n'),
        ('\t\t\treturn\tGetClassInfoAs( L"String" ) ;\n', '\t\t}\n\t}\n'),
        ('\t\t\t\t\t\t\tSetDefaultElement( typeContainer.DetachValue() ) ;\n\t\t\t\t\t\tbreak ;\n', '\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t\telse\n'),
        ('\t\treturn\tnew ECSInteger( 0, 0xFFFFFFFF ) ;\n', '\t}\n\treturn\tNULL ;\n'),
        ('\t\t\tbreak ;\n', '\t\t}\n\t\treturn\tESLErrorMsg( "naked モードで不正な定数型です。" ) ;\n'),
        ('\t\tbreak ;\n', '\t}\n\tpClassInf->AddOverrideFunction( prototype ) ;\n'),
        ('\t\t\t\t\tpInitElement = pVarType->m_pValue ;\n', '\t\t\t\t}\n\t\t\t}\n'),
        ('\t\t\treturn\toppShift ;\n', '\t\t}\n\t\tbreak ;\n'),
        ('\t\treturn\toppNew ;\n', '\t}\n\treturn\toppNothing ;\n'),
        ('\t\t\treturn\toppUnary ;\n', '\t\t}\n\t\tbreak ;\n'),
        ('\t\t\treturn\toppExUnary ;\n', '\t\t}\n\t\tbreak ;\n'),
    ),
    'glscs_context.cpp': (
        ('\t\treturn\tnew_CSPointerReference() ;\n', '\t}\n\tECSObject *\tpObj = CreateExtendedObject( csvtType, pwszType, pdwFuncAddr ) ;\n'),
        ('\t\tbreak ;\n', '\t}\n\tECSClassInfo *\tpClassInfo = m_pcsxi->GetClassInfoAs( pwszType ) ;\n'),
        ('\t\t\tbufArg.Write( &ptrBuf, sizeof(ptrBuf) ) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\tlong int\tnRetValue ;\n'),
        ('\t\t\t\t\t\tbreak ;\n', '\t\t\t\t\t}\n\t\t\t\t}\n'),
        ('\t\t\t\t\t\t( fptr.m_varFunc.pfnNaked, m_arg.m_varArray ) ;\n', '\t\t}\n\t}\n'),
    ),
    'glscs_execution_image.cpp': (
        ('\t\t\t\treturn\teslErrGeneral ;\n\t\t\t}\n\t\t}\n\t\tbreak ;\n', '\t}\n\treturn\teslErrSuccess ;\n}\n\n// 型情報を書き出す\n'),
        ('\t\t\t\treturn\terr ;\n\t\t\t}\n\t\t}\n\t\tbreak ;\n', '\t}\n\treturn\teslErrSuccess ;\n}\n\nESLError ECSExecutionImage::WriteTypeInfo\n'),
        ('\t\t\t}\n\t\t\tbreak ;\n', '\t\t}\n\t}\n}\n'),
    ),
    'glscs_execution_image_compiler.cpp': (
        ('\t\t\tbreak ;\n', '\t\t}\n\t\tif ( icNotLogical != ECSSakura2Processor::codeInvalid )\n'),
    ),
    'glscs_rosetta.cpp': (
        ('\t\tbreak ;\n', '\t}\n\treturn\tNULL ;\n'),
    ),
    'glscsobj_buffer_structure.cpp': (
        ('\t\t\t\tFlushBuffer( iOffset, nSize, ptrBuf, false ) ;\n\t\t\t}\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\twhile ( false ) ;\n\treturn\tnValue ;\n'),
        ('\t\t\t\tFlushBuffer( iOffset, nSize, ptrBuf, false ) ;\n\t\t\t}\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\twhile ( false ) ;\n\treturn\trValue ;\n'),
        ('\t\t\t\t\t( ptrBuf, nValue,\n\t\t\t\t\t\t((ECSReal*)pTypeObj)->m_vtRealType ) ;\n\t\t\t\tFlushBuffer( iOffset, nSize, ptrBuf, true ) ;\n\t\t\t}\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\twhile ( false ) ;\n}\n\n// 実数のメンバ変数を設定する\n'),
        ('\t\t\t\t\t( ptrBuf, rValue,\n\t\t\t\t\t\t((ECSReal*)pTypeObj)->m_vtRealType ) ;\n\t\t\t\tFlushBuffer( iOffset, nSize, ptrBuf, true ) ;\n\t\t\t}\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\twhile ( false ) ;\n}\n\n// オブジェクトの型名を取得する\n'),
    ),
    'glscsobj_hash.cpp': (
        ('\t\t\treturn\tcss.GetInteger( ) ;\n', '\t\t}\n\t}\n'),
        ('\t\t\treturn\tcss.GetRealNumber( ) ;\n', '\t\t}\n\t}\n'),
        ('\t\t\treturn\t(double) ((ECSReal*)pObj)->m_varReal ;\n', '\t\t}\n\t}\n'),
    ),
    'glscsobj_integer.cpp': (
        ('\t\t\tbreak ;\n', '\t\t}\n\t\treturn\teslErrSuccess ;\n'),
        ('\t\tbreak ;\n', '\t}\n\treturn\teslErrSuccess ;\n'),
    ),
    'glscsobj_pointer.cpp': (
        ('\t\t\tnResult = - (int) (nValue0 > nValue1) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse\n'),
        ('\t\t\tnResult = - (int) (nValue0 > nValue1) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\treturn\teslErrSuccess ;\n'),
        ('\t\tbreak ;\n', '\t}\n\t//\n'),
    ),
    'glscsobj_real.cpp': (
        ('\t\t\tnResult = - (int) (m_varReal >= rVal) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse\n'),
        ('\t\t\tnResult = - (int) (m_varReal > rVal) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\treturn\teslErrSuccess ;\n'),
    ),
    'glscsobj_reference.cpp': (
        ('\t\t\t\t\tbreak ;\n', '\t\t\t\t}\n\t\t\t\tfor ( int i = 0; i < (int) m_dimIndex.GetSize(); i ++ )\n'),
    ),
    'glscsobj_string.cpp': (
        ('\t\t\tnResult = - (int) (m_varStr >= pStr->m_varStr) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse\n'),
        ('\t\t\tnResult = - (int) (m_varStr > pStr->m_varStr) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\treturn\teslErrSuccess ;\n'),
        ('\t\t\t\tbreak ;\n', '\t\t\t}\n\t\t}\n'),
    ),
    'glscsobj_structure.cpp': (
        ('\t\t\treturn\t((ECSString*)pObj)->m_varStr.AsInteger() ;\n', '\t\t}\n\t}\n'),
        ('\t\t\treturn\t((ECSString*)pObj)->m_varStr.AsReal() ;\n', '\t\t}\n\t}\n'),
        ('\t\t\treturn\t((ECSString*)pObj)->m_varStr ;\n', '\t\t}\n\t}\n'),
        ('\t\t\t((ECSString*)pObj)->m_varStr.FromInteger( nValue ) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse if ( m_pClassInf == NULL )\n'),
        ('\t\t\t((ECSString*)pObj)->m_varStr.FromReal( rValue ) ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse if ( m_pClassInf == NULL )\n'),
        ('\t\t\t((ECSString*)pObj)->m_varStr = pwszValue ;\n\t\t\tbreak ;\n', '\t\t}\n\t}\n\telse if ( m_pClassInf == NULL )\n'),
    ),
}


def patch_source(relative_path: str, text: str) -> str:
    for before, after in _SWITCH_ENDS.get(Path(relative_path).name, ()):
        anchor = before + after
        if text.count(anchor) != 1:
            raise ValueError(f"legacy switch anchor changed in {relative_path}: {anchor!r}")
        indentation = after.partition("}")[0]
        text = text.replace(anchor, before + indentation + "default:\n" + indentation + "\tbreak ;\n" + after)
    return text
