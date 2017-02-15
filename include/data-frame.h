/*! \file data-frame.h
 *
 * Storage class used to represent a single chunk of data sent
 * from the BLDS to remote clients.
 *
 * (C) 2017 Benjamin Naecker bnaecker@stanford.edu
 */

#ifndef BLDS_DATA_FRAME_H
#define BLDS_DATA_FRAME_H

#include <armadillo>

#include <QtCore>

/*! \class DataFrame
 * The DataFrame class represents a chunk of data from a data source. It 
 * includes the time in the data stream of the start and stop of the source,
 * as floating point values. This is primarily used to send data to
 * remote clients by the BLDS.
 */
class DataFrame {

	public:

		/*! Type alias for data from the source. */
		using DataType = qint16;

		/*! Type alias for a chunk of data. */
		using Samples = arma::Mat<DataType>;

		/*! Construct an empty frame. */
		DataFrame() { }

		/*! Destroy a frame. */
		~DataFrame() { }

		/*! Construct a frame.
		 * \param start The start time of this chunk of data.
		 * \param stop The stop time of this chunk of data.
		 * \param data The actual samples of data, of shape (nsamples, nchannels).
		 */
		DataFrame(double start, double stop, const Samples& data) :
			m_start(start),
			m_stop(stop),
			m_data(data)
		{
		}

		/*! Copy-assign a data frame. */
		DataFrame& operator=(DataFrame other)
		{
			swap(*this, other);
			return *this;
		}

		/*! Copy construct a data frame. */
		DataFrame(const DataFrame& other) :
			m_start(other.m_start),
			m_stop(other.m_stop),
			m_data(other.m_data)
		{
		}

		/*! Move-construct a data frame. */
		DataFrame(DataFrame&& other)
		{
			swap(*this, other);
		}

		/*! Swap two data frames. */
		friend void swap(DataFrame& first, DataFrame& second)
		{
			using std::swap;
			swap(first.m_start, second.m_start);
			swap(first.m_stop, second.m_stop);
			first.m_data.swap(second.m_data);
		}

		/*! Return the start time of this frame. */
		double start() const
		{
			return m_start;
		}

		/*! Return the stop time of this frame. */
		double stop() const
		{
			return m_stop;
		}

		/*! Return the actual data of this frame. */
		const Samples& data() const
		{
			return m_data;
		}
	
		/*! Return the number of channels of data in this frame. */
		quint32 nchannels() const
		{
			return m_data.n_cols;
		}

		/*! Return the number of samples of data in this frame. */
		quint32 nsamples() const
		{
			return m_data.n_rows;
		}

		/*! Serialize this frame to an array of bytes.
		 * This is used to send frames to remote clients.
		 * Data is serialize in the following way:
		 * 	- start time (double)
		 * 	- stop time (double)
		 * 	- number of samples (uint32_t)
		 * 	- number of channels (uint32_t)
		 * 	- actual data (array of int16_t)
		 */
		QByteArray serialize() const
		{
			QByteArray ba;
			ba.reserve(2 * sizeof(m_start) + 2 * sizeof(quint32) * 
					m_data.n_elem * sizeof(DataType));
			ba.append(reinterpret_cast<const char*>(&m_start), sizeof(m_start));
			ba.append(reinterpret_cast<const char*>(&m_stop), sizeof(m_stop));
			auto nsamp = nsamples();
			ba.append(reinterpret_cast<const char*>(&nsamp), sizeof(nsamp));
			auto nchan = nchannels();
			ba.append(reinterpret_cast<const char*>(&nchan), sizeof(nchan));
			ba.append(reinterpret_cast<const char*>(m_data.memptr()), 
					m_data.n_elem * sizeof(DataType));
			return ba;
		}

	private:
		double m_start;
		double m_stop;
		Samples m_data;
};

#endif

