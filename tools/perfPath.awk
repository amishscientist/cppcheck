#! /usr/bin/awk -f

/ValueFlow::setValues;valueFlowAfterAssign/ {cntvalueFlowAfterAssign=cntvalueFlowAfterAssign+$NF;next;}
/ValueFlow::setValues;valueFlowAfterMove/ {cntvalueFlowAfterMove=cntvalueFlowAfterMove+$NF;next;}
/ValueFlow::setValues;valueFlowAfterSwap/ {cntvalueFlowAfterSwap=cntvalueFlowAfterSwap+$NF;next;}
/ValueFlow::setValues;valueFlowArrayBool/ {cntvalueFlowArrayBool=cntvalueFlowArrayBool+$NF;next;}
/ValueFlow::setValues;valueFlowArray/ {cntvalueFlowArray=cntvalueFlowArray+$NF;next;}
/ValueFlow::setValues;valueFlowBitAnd/ {cntvalueFlowBitAnd=cntvalueFlowBitAnd+$NF;next;}
/ValueFlow::setValues;valueFlowConditionExpressions/ {cntvalueFlowConditionExpressions=cntvalueFlowConditionExpressions+$NF;next;}
/ValueFlow::setValues;valueFlowContainerSize/ {cntvalueFlowContainerSize=cntvalueFlowContainerSize+$NF;next;}
/ValueFlow::setValues;valueFlowDynamicBufferSize/ {cntvalueFlowDynamicBufferSize=cntvalueFlowDynamicBufferSize+$NF;next;}
/ValueFlow::setValues;valueFlowEnumValue/ {cntvalueFlowEnumValue=cntvalueFlowEnumValue+$NF;next;}
/ValueFlow::setValues;valueFlowForLoop/ {cntvalueFlowForLoop=cntvalueFlowForLoop+$NF;next;}
/ValueFlow::setValues;valueFlowFunctionDefaultParameter/ {cntvalueFlowFunctionDefaultParameter=cntvalueFlowFunctionDefaultParameter+$NF;next;}
/ValueFlow::setValues;valueFlowFunctionReturn/ {cntvalueFlowFunctionReturn=cntvalueFlowFunctionReturn+$NF;next;}
/ValueFlow::setValues;valueFlowGlobalConstVar/ {cntvalueFlowGlobalConstVar=cntvalueFlowGlobalConstVar+$NF;next;}
/ValueFlow::setValues;valueFlowGlobalStaticVar/ {cntvalueFlowGlobalStaticVar=cntvalueFlowGlobalStaticVar+$NF;next;}
/ValueFlow::setValues;valueFlowImpossibleValues/ {cntvalueFlowImpossibleValues=cntvalueFlowImpossibleValues+$NF;next;}
/ValueFlow::setValues;valueFlowInferCondition/ {cntvalueFlowInferCondition=cntvalueFlowInferCondition+$NF;next;}
/ValueFlow::setValues;valueFlowIteratorInfer/ {cntvalueFlowIteratorInfer=cntvalueFlowIteratorInfer+$NF;next;}
/ValueFlow::setValues;valueFlowIterators/ {cntvalueFlowIterators=cntvalueFlowIterators+$NF;next;}
/ValueFlow::setValues;valueFlowLifetime/ {cntvalueFlowLifetime=cntvalueFlowLifetime+$NF;next;}
/ValueFlow::setValues;valueFlowNumber/ {cntvalueFlowNumber=cntvalueFlowNumber+$NF;next;}
/ValueFlow::setValues;valueFlowPointerAlias/ {cntvalueFlowPointerAlias=cntvalueFlowPointerAlias+$NF;next;}
/ValueFlow::setValues;valueFlowRightShift/ {cntvalueFlowRightShift=cntvalueFlowRightShift+$NF;next;}
/ValueFlow::setValues;valueFlowSafeFunctions/ {cntvalueFlowSafeFunctions=cntvalueFlowSafeFunctions+$NF;next;}
/ValueFlow::setValues;valueFlowSameExpressions/ {cntvalueFlowSameExpressions=cntvalueFlowSameExpressions+$NF;next;}
/ValueFlow::setValues;valueFlowSmartPointer/ {cntvalueFlowSmartPointer=cntvalueFlowSmartPointer+$NF;next;}
/ValueFlow::setValues;valueFlowString/ {cntvalueFlowString=cntvalueFlowString+$NF;next;}
/ValueFlow::setValues;valueFlowSubFunction/ {cntvalueFlowSubFunction=cntvalueFlowSubFunction+$NF;next;}
/ValueFlow::setValues;valueFlowSwitchVariable/ {cntvalueFlowSwitchVariable=cntvalueFlowSwitchVariable+$NF;next;}
/ValueFlow::setValues;valueFlowSymbolicAbs/ {cntvalueFlowSymbolicAbs=cntvalueFlowSymbolicAbs+$NF;next;}
/ValueFlow::setValues;valueFlowSymbolicIdentity/ {cntvalueFlowSymbolicIdentity=cntvalueFlowSymbolicIdentity+$NF;next;}
/ValueFlow::setValues;valueFlowSymbolicInfer/ {cntvalueFlowSymbolicInfer=cntvalueFlowSymbolicInfer+$NF;next;}
/ValueFlow::setValues;valueFlowSymbolic/ {cntvalueFlowSymbolic=cntvalueFlowSymbolic+$NF;next;}
/ValueFlow::setValues;valueFlowUninit/ {cntvalueFlowUninit=cntvalueFlowUninit+$NF;next;}
/ValueFlow::setValues;valueFlowUnknownFunctionReturn/ {cntvalueFlowUnknownFunctionReturn=cntvalueFlowUnknownFunctionReturn+$NF;next;}
/ValueFlow::setValues;WR01valueFlowCondition/ {cntWR01valueFlowCondition=cntWR01valueFlowCondition+$NF;next;}
/ValueFlow::setValues;WR02valueFlowCondition/ {cntWR02valueFlowCondition=cntWR02valueFlowCondition+$NF;next;}
/ValueFlow::setValues;WR03valueFlowCondition/ {cntWR03valueFlowCondition=cntWR03valueFlowCondition+$NF;next;}
/ValueFlow::setValues;WR04valueFlowCondition/ {cntWR04valueFlowCondition=cntWR04valueFlowCondition+$NF;next;}
/ValueFlow::setValues;/ {cntOthersetValues=cntOthersetValues+$NF;next;}
/ValueFlow::setValues/ {cntsetValues=cntsetValues+$NF;next;}
END{
	print cntsetValues" : setValues";
        print cntvalueFlowAfterAssign" : valueFlowAfterAssign";
        print cntvalueFlowAfterMove" : valueFlowAfterMove";
        print cntvalueFlowAfterSwap" : valueFlowAfterSwap";
        print cntvalueFlowArrayBool" : valueFlowArrayBool";
        print cntvalueFlowArray" : valueFlowArray";
        print cntvalueFlowBitAnd" : valueFlowBitAnd";
        print cntvalueFlowConditionExpressions" : valueFlowConditionExpressions";
        print cntvalueFlowContainerSize" : valueFlowContainerSize";
        print cntvalueFlowDynamicBufferSize" : valueFlowDynamicBufferSize";
        print cntvalueFlowEnumValue" : valueFlowEnumValue";
        print cntvalueFlowForLoop" : valueFlowForLoop";
        print cntvalueFlowFunctionDefaultParameter" : valueFlowFunctionDefaultParameter";
        print cntvalueFlowFunctionReturn" : valueFlowFunctionReturn";
        print cntvalueFlowGlobalConstVar" : valueFlowGlobalConstVar";
        print cntvalueFlowGlobalStaticVar" : valueFlowGlobalStaticVar";
        print cntvalueFlowImpossibleValues" : valueFlowImpossibleValues";
        print cntvalueFlowInferCondition" : valueFlowInferCondition";
        print cntvalueFlowIteratorInfer" : valueFlowIteratorInfer";
        print cntvalueFlowIterators" : valueFlowIterators";
        print cntvalueFlowLifetime" : valueFlowLifetime";
        print cntvalueFlowNumber" : valueFlowNumber";
        print cntvalueFlowPointerAlias" : valueFlowPointerAlias";
        print cntvalueFlowRightShift" : valueFlowRightShift";
        print cntvalueFlowSafeFunctions" : valueFlowSafeFunctions";
        print cntvalueFlowSameExpressions" : valueFlowSameExpressions";
        print cntvalueFlowSmartPointer" : valueFlowSmartPointer";
        print cntvalueFlowString" : valueFlowString";
        print cntvalueFlowSubFunction" : valueFlowSubFunction";
        print cntvalueFlowSwitchVariable" : valueFlowSwitchVariable";
        print cntvalueFlowSymbolicAbs" : valueFlowSymbolicAbs";
        print cntvalueFlowSymbolicIdentity" : valueFlowSymbolicIdentity";
        print cntvalueFlowSymbolicInfer" : valueFlowSymbolicInfer";
        print cntvalueFlowSymbolic" : valueFlowSymbolic";
        print cntvalueFlowUninit" : valueFlowUninit";
        print cntvalueFlowUnknownFunctionReturn" : valueFlowUnknownFunctionReturn";
        print cntWR01valueFlowCondition" : WR01valueFlowCondition";
        print cntWR02valueFlowCondition" : WR02valueFlowCondition";
        print cntWR03valueFlowCondition" : WR03valueFlowCondition";
        print cntWR04valueFlowCondition" : WR04valueFlowCondition";

}
