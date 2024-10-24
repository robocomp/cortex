``` bash
cd ~/software/Fast-DDS-Gen
git pull
./gradlew clean
./gradlew jar
mv ./build/libs/fastddsgen.jar ./share/fastddsgen/java/fastddsgen.jar
scripts/fastddsgen ~/robocomp_ws/src/robocomp/robocomp_cortex/core/topics/IDLGraph.idl -no-typeobjectsupport
sed -i 's\IDLGraphCdrAux.hpp\dsr/core/topics/IDLGraphCdrAux.hpp\g' IDLGraphCdrAux.ipp
sed -i 's\IDLGraphPubSubTypes.hpp\dsr/core/topics/IDLGraphPubSubTypes.hpp\g' IDLGraphPubSubTypes.cxx
sed -i 's\IDLGraphCdrAux.hpp\dsr/core/topics/IDLGraphCdrAux.hpp\g' IDLGraphPubSubTypes.cxx
sed -i 's/\(const \)\(.*\&\) data/\1IDL::\2 data/' IDLGraphCdrAux.hpp
sed -i '31s/^/using namespace IDL;/' IDLGraphPubSubTypes.cxx #reemplazar 31 con un numero de linea vacio
sed -i 's\typedef \typedef IDL::\g' IDLGraphPubSubTypes.hpp
sed -i '60s/^/namespace IDL{/' IDLGraph.hpp # reemplazar 60 con un numero de linea justo antes de la clase Val
sed -i "$(wc -l IDLGraph.hpp | awk '{print $1-3}')s/$/}\n/" IDLGraph.hpp # pone el cierre del namespace } tres lineas antes del final del fichero.
cp *.hpp ~/robocomp_ws/src/robocomp/robocomp_cortex/core/include/dsr/core/topics/
cp *cxx ~/robocomp_ws/src/robocomp/robocomp_cortex/core/topics/
cp *ipp ~/robocomp_ws/src/robocomp/robocomp_cortex/core/topics/

```

En la clase PairInt en /core/include/dsr/core/topics/IDLGraph.hpp

``` cpp
    eProsima_user_DllExport bool operator<(
            const PairInt &rhs) const
    {
        if (m_first < rhs.m_first)
            return true;
        if (rhs.m_first < m_first)
            return false;
        return m_second < rhs.m_second;
    }
```

En la clase EdgeKey en /core/include/dsr/core/topics/IDLGraph.hpp

```cpp

    eProsima_user_DllExport bool operator<(
        const EdgeKey &rhs) const
    {
        if (m_to < rhs.m_to)
            return true;
        if (rhs.m_to < m_to)
            return false;
        return m_type < rhs.m_type;
    }
```

