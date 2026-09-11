inline Alias::Alias( Type type ) : m_type(type), m_id(s_nextId++), m_allocatedRegister(NULL), m_sameNamePredecessor(NULL)
{
}

inline void Alias::setSameNamePredecessor( Alias* predecessor )
{
	m_sameNamePredecessor = predecessor;
}

inline Alias* Alias::sameNamePredecessor() const
{
	return m_sameNamePredecessor;
}

inline void Alias::setDebugName( const std::string& name )
{
	if( m_debugName.empty() )
		m_debugName = name;
}

inline const std::string& Alias::debugName() const
{
	return m_debugName;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void Alias::setAllocatedRegister( const Register* allocated )
{
	m_allocatedRegister = allocated;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline const Register* Alias::allocatedRegister() const
{
	return m_allocatedRegister;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline unsigned int Alias::id() const
{
	return m_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline Alias::Type Alias::type() const
{
	return m_type;
}
